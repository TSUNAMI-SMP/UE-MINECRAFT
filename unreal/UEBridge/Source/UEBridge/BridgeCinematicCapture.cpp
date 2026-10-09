#include "BridgeCinematicCapture.h"
#include "BridgeReceiver.h"
#include "BridgeRealisticWorld.h"
#include "BridgeRealisticExplosion.h"
#include "BridgeNativeInventory.h"
#include "BridgeNativePlayerController.h"
#include "BridgeCharacter.h"
#include "BridgeMobCharacter.h"
#include "BridgeMobData.h"
#include "BridgeMobWorld.h"
#include "BridgeDroppedItem.h"
#include "BridgeBlockPreview.h"
#include "BridgeWorld.h"
#include "BridgeVideo.h"
#include "BridgeNativeFile.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Kismet/GameplayStatics.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "RenderingThread.h"

namespace {
constexpr int32 MaxObservationBytes=16*1024*1024;
constexpr int64 MaxTapeBytes=512LL*1024*1024;
TArray<TSharedPtr<FJsonValue>> V(const FVector& P) {return {MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};}
bool Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FVector& P) {
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O->TryGetArrayField(Key,A)||A->Num()!=3) return false;
    double X,Y,Z;if(!(*A)[0]->TryGetNumber(X)||!(*A)[1]->TryGetNumber(Y)||!(*A)[2]->TryGetNumber(Z)||!FMath::IsFinite(X)||!FMath::IsFinite(Y)||!FMath::IsFinite(Z)||FMath::Max3(FMath::Abs(X),FMath::Abs(Y),FMath::Abs(Z))>1e8) return false;
    P=FVector(X,Y,Z);return true;
}
FString Json(const TSharedPtr<FJsonObject>& O) {FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&S));return S;}
FString ActorKey(AActor* A) {if(auto* M=Cast<ABridgeMobCharacter>(A)) return TEXT("mob:")+M->MinecraftId;if(A->IsA<ABridgeCharacter>()) return TEXT("player");return A->GetName();}
bool Observed(AActor* A) {return A->IsA<ABridgeCharacter>()||A->IsA<ABridgeMobCharacter>()||A->IsA<ABridgeDroppedItem>();}
TSharedPtr<FJsonObject> Transform(const FTransform& T) {auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("p"),V(T.GetLocation()));O->SetArrayField(TEXT("r"),V(FVector(T.Rotator().Pitch,T.Rotator().Yaw,T.Rotator().Roll)));O->SetArrayField(TEXT("s"),V(T.GetScale3D()));return O;}
bool ReadTransform(const TSharedPtr<FJsonObject>& O,FTransform& T) {FVector P,R,S;if(!Vector(O,TEXT("p"),P)||!Vector(O,TEXT("r"),R)||!Vector(O,TEXT("s"),S)||S.GetAbsMax()>1000) return false;T=FTransform(FRotator(R.X,R.Y,R.Z),P,S);return true;}
TSharedPtr<FJsonObject> CapturePlayerMeshes(AActor* Actor) {
    auto Out=MakeShared<FJsonObject>();TInlineComponentArray<UProceduralMeshComponent*> Meshes;Actor->GetComponents(Meshes);
    for(auto* Mesh:Meshes) {TArray<TSharedPtr<FJsonValue>> Sections;
        for(int32 Index=0;Index<Mesh->GetNumSections();++Index) if(const auto* Part=Mesh->GetProcMeshSection(Index)) {
            auto S=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Vertices,Triangles;
            for(const auto& P:Part->ProcVertexBuffer) {
                TArray<TSharedPtr<FJsonValue>> Row;
                for(double N:{double(P.Position.X),double(P.Position.Y),double(P.Position.Z),double(P.Normal.X),double(P.Normal.Y),double(P.Normal.Z),double(P.UV0.X),double(P.UV0.Y),double(P.Color.R),double(P.Color.G),double(P.Color.B),double(P.Color.A),double(P.Tangent.TangentX.X),double(P.Tangent.TangentX.Y),double(P.Tangent.TangentX.Z),P.Tangent.bFlipTangentY?1.:0.}) Row.Add(MakeShared<FJsonValueNumber>(N));
                Vertices.Add(MakeShared<FJsonValueArray>(Row));
            }
            for(uint32 I:Part->ProcIndexBuffer) Triangles.Add(MakeShared<FJsonValueNumber>(I));
            S->SetArrayField(TEXT("vertices"),Vertices);S->SetArrayField(TEXT("triangles"),Triangles);
            auto* Material=Mesh->GetMaterial(Index);if(Material) {
                auto* Dynamic=Cast<UMaterialInstanceDynamic>(Material);S->SetStringField(TEXT("material"),(Dynamic&&Dynamic->Parent?Dynamic->Parent.Get():Material)->GetPathName());
                auto Textures=MakeShared<FJsonObject>(),Scalars=MakeShared<FJsonObject>(),Vectors=MakeShared<FJsonObject>();TArray<FMaterialParameterInfo> Infos;TArray<FGuid> Guids;
                Material->GetAllTextureParameterInfo(Infos,Guids);for(const auto& Info:Infos) {UTexture* Texture=nullptr;if(Material->GetTextureParameterValue(Info,Texture)&&Texture) Textures->SetStringField(Info.Name.ToString(),Texture->GetPathName());}
                Infos.Reset();Guids.Reset();Material->GetAllScalarParameterInfo(Infos,Guids);for(const auto& Info:Infos) {float Value;if(Material->GetScalarParameterValue(Info,Value)) Scalars->SetNumberField(Info.Name.ToString(),Value);}
                Infos.Reset();Guids.Reset();Material->GetAllVectorParameterInfo(Infos,Guids);for(const auto& Info:Infos) {FLinearColor Color;if(Material->GetVectorParameterValue(Info,Color)) Vectors->SetArrayField(Info.Name.ToString(),{MakeShared<FJsonValueNumber>(Color.R),MakeShared<FJsonValueNumber>(Color.G),MakeShared<FJsonValueNumber>(Color.B),MakeShared<FJsonValueNumber>(Color.A)});}
                S->SetObjectField(TEXT("textures"),Textures);S->SetObjectField(TEXT("scalars"),Scalars);S->SetObjectField(TEXT("vectors"),Vectors);
            }
            Sections.Add(MakeShared<FJsonValueObject>(S));
        }
        Out->SetArrayField(Mesh->GetName(),Sections);
    }
    return Out;
}
bool ApplyPlayerMeshes(AActor* Actor,const TSharedPtr<FJsonObject>& MeshData,const TSharedPtr<FJsonObject>& Upcoming,float Alpha) {
    TInlineComponentArray<UProceduralMeshComponent*> Meshes;Actor->GetComponents(Meshes);int32 Total=0;
    for(auto* Mesh:Meshes) {
        const TArray<TSharedPtr<FJsonValue>> *Sections=nullptr,*NextSections=nullptr;if(!MeshData->TryGetArrayField(Mesh->GetName(),Sections)) continue;
        if(Sections->Num()>64) return false;if(Upcoming.IsValid()) Upcoming->TryGetArrayField(Mesh->GetName(),NextSections);
        Mesh->ClearAllMeshSections();
        for(int32 Index=0;Index<Sections->Num();++Index) {
            const auto& Value=(*Sections)[Index];if(!Value.IsValid()||Value->Type!=EJson::Object) return false;auto S=Value->AsObject();
            const TArray<TSharedPtr<FJsonValue>> *Vertices=nullptr,*Triangles=nullptr,*NextVertices=nullptr;
            if(!S->TryGetArrayField(TEXT("vertices"),Vertices)||!S->TryGetArrayField(TEXT("triangles"),Triangles)||Vertices->Num()>65536||Triangles->Num()>393216||Triangles->Num()%3!=0) return false;
            Total+=Vertices->Num();if(Total>131072) return false;
            if(NextSections&&NextSections->IsValidIndex(Index)&&(*NextSections)[Index].IsValid()&&(*NextSections)[Index]->Type==EJson::Object) {
                auto Future=(*NextSections)[Index]->AsObject();const TArray<TSharedPtr<FJsonValue>>* FutureTriangles=nullptr;
                FString Material,NextMaterial;const TSharedPtr<FJsonObject> *Textures=nullptr,*NextTextures=nullptr;
                bool Same=S->TryGetStringField(TEXT("material"),Material)&&Future->TryGetStringField(TEXT("material"),NextMaterial)&&Material==NextMaterial
                    &&Future->TryGetArrayField(TEXT("triangles"),FutureTriangles)&&FutureTriangles->Num()==Triangles->Num();
                if(Same && S->TryGetObjectField(TEXT("textures"),Textures)) Same=Future->TryGetObjectField(TEXT("textures"),NextTextures)&&Json(*Textures)==Json(*NextTextures);
                if(Same) for(int32 I=0;I<Triangles->Num();++I) {double A,B;if(!(*Triangles)[I]->TryGetNumber(A)||!(*FutureTriangles)[I]->TryGetNumber(B)||A!=B) {Same=false;break;}}
                // Changing a held item is a discrete event. Do not morph two
                // unrelated models merely because their vertex counts match.
                if(Same) Future->TryGetArrayField(TEXT("vertices"),NextVertices);
            }
            TArray<FVector> Positions,Normals;TArray<FVector2D> UV;TArray<FColor> Colors;TArray<FProcMeshTangent> Tangents;TArray<int32> Indices;
            for(int32 I=0;I<Vertices->Num();++I) {
                const auto& Row=(*Vertices)[I];if(!Row.IsValid()||Row->Type!=EJson::Array||Row->AsArray().Num()!=16) return false;
                double N[16];for(int C=0;C<16;++C) if(!Row->AsArray()[C]->TryGetNumber(N[C])||!FMath::IsFinite(N[C])||FMath::Abs(N[C])>1e8) return false;
                if(NextVertices&&NextVertices->Num()==Vertices->Num()&&(*NextVertices)[I].IsValid()&&(*NextVertices)[I]->Type==EJson::Array&&(*NextVertices)[I]->AsArray().Num()==16)
                    for(int C=0;C<6;++C) {double Next;if(!(*NextVertices)[I]->AsArray()[C]->TryGetNumber(Next)||!FMath::IsFinite(Next)) return false;N[C]=FMath::Lerp(N[C],Next,double(Alpha));}
                for(int C=8;C<12;++C) if(N[C]<0||N[C]>255||N[C]!=FMath::FloorToDouble(N[C])) return false;
                Positions.Add(FVector(N[0],N[1],N[2]));Normals.Add(FVector(N[3],N[4],N[5]).GetSafeNormal());UV.Add(FVector2D(N[6],N[7]));Colors.Add(FColor(uint8(N[8]),uint8(N[9]),uint8(N[10]),uint8(N[11])));Tangents.Add(FProcMeshTangent(FVector(N[12],N[13],N[14]),N[15]!=0));
            }
            for(const auto& Triangle:*Triangles) {double N;if(!Triangle->TryGetNumber(N)||N<0||N>=Vertices->Num()||N!=FMath::FloorToDouble(N)) return false;Indices.Add(int32(N));}
            Mesh->CreateMeshSection(Index,Positions,Indices,Normals,UV,Colors,Tangents,false);
            FString Parent;const TSharedPtr<FJsonObject> *Textures=nullptr,*Scalars=nullptr,*Vectors=nullptr;
            if(S->TryGetStringField(TEXT("material"),Parent)) {
                if(!Parent.StartsWith(TEXT("/Game/Bridge/"))&&!Parent.StartsWith(TEXT("/Engine/"))) return false;
                auto* Base=LoadObject<UMaterialInterface>(nullptr,*Parent);if(!Base) return false;
                auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Index));
                if(!Material||Material->Parent.Get()!=Base) Material=UMaterialInstanceDynamic::Create(Base,Actor);
                if(S->TryGetObjectField(TEXT("textures"),Textures)) {if((*Textures)->Values.Num()>64) return false;for(const auto& Entry:(*Textures)->Values) {FString Path;if(!Entry.Value->TryGetString(Path)||(!Path.StartsWith(TEXT("/Game/Bridge/"))&&!Path.StartsWith(TEXT("/Engine/")))) return false;auto* Texture=LoadObject<UTexture>(nullptr,*Path);if(!Texture) return false;Material->SetTextureParameterValue(FName(*Entry.Key),Texture);}}
                if(S->TryGetObjectField(TEXT("scalars"),Scalars)) {if((*Scalars)->Values.Num()>64) return false;for(const auto& Entry:(*Scalars)->Values) {double N;if(!Entry.Value->TryGetNumber(N)||!FMath::IsFinite(N)) return false;Material->SetScalarParameterValue(FName(*Entry.Key),float(N));}}
                if(S->TryGetObjectField(TEXT("vectors"),Vectors)) {if((*Vectors)->Values.Num()>64) return false;for(const auto& Entry:(*Vectors)->Values) {if(Entry.Value->Type!=EJson::Array||Entry.Value->AsArray().Num()!=4) return false;double N[4];for(int C=0;C<4;++C) if(!Entry.Value->AsArray()[C]->TryGetNumber(N[C])||!FMath::IsFinite(N[C])) return false;Material->SetVectorParameterValue(FName(*Entry.Key),FLinearColor(N[0],N[1],N[2],N[3]));}}
                Mesh->SetMaterial(Index,Material);
            }
        }
    }
    return true;
}
TSharedPtr<FJsonObject> CellSnapshot(ABridgeWorld* World,const FIntVector& Key) {
    TArray<FBridgeBlock> Rows;TArray<uint8> Sky;TArray<uint16> Water;if(!World->GetNativeCell(Key,Rows,Sky)) return nullptr;World->GetNativeWaterCell(Key,Water);
    auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("key"),V(FVector(Key)));TArray<TSharedPtr<FJsonValue>> Blocks;
    for(const auto& B:Rows) {auto R=MakeShared<FJsonObject>();R->SetArrayField(TEXT("p"),V(B.Position));R->SetArrayField(TEXT("s"),V(B.Size));R->SetArrayField(TEXT("source"),V(FVector(B.SourceBlock)));R->SetStringField(TEXT("id"),B.BlockId);R->SetStringField(TEXT("state"),B.StateKey);R->SetNumberField(TEXT("tint"),B.Color);R->SetBoolField(TEXT("collision"),B.Collision);R->SetNumberField(TEXT("role"),B.Role);R->SetBoolField(TEXT("light"),B.HasLight);R->SetBoolField(TEXT("compact"),B.NativeCompact);R->SetNumberField(TEXT("sky"),B.SkyLight);R->SetNumberField(TEXT("block"),B.BlockLight);R->SetNumberField(TEXT("emission"),B.Emission);R->SetNumberField(TEXT("opacity"),B.Opacity);Blocks.Add(MakeShared<FJsonValueObject>(R));}
    TArray<TSharedPtr<FJsonValue>> W,S;for(auto I:Water) W.Add(MakeShared<FJsonValueNumber>(I));for(auto I:Sky) S.Add(MakeShared<FJsonValueNumber>(I));
    O->SetArrayField(TEXT("rows"),Blocks);O->SetArrayField(TEXT("water"),W);O->SetArrayField(TEXT("sky"),S);return O;
}
bool ApplyCell(ABridgeWorld* World,const TSharedPtr<FJsonObject>& O) {
    FVector Key;const TArray<TSharedPtr<FJsonValue>> *Rows=nullptr,*Water=nullptr,*Sky=nullptr;
    if(!Vector(O,TEXT("key"),Key)||!O->TryGetArrayField(TEXT("rows"),Rows)||Rows->Num()>8192||!O->TryGetArrayField(TEXT("water"),Water)||Water->Num()>512||!O->TryGetArrayField(TEXT("sky"),Sky)||Sky->Num()>64) return false;
    auto Int=[](const TSharedPtr<FJsonObject>& R,const TCHAR* Field,int Min,int Max,int& Out){double N;if(!R->TryGetNumberField(Field,N)||!FMath::IsFinite(N)||N<Min||N>Max||N!=FMath::FloorToDouble(N)) return false;Out=int(N);return true;};
    if(Key!=FVector(FIntVector(Key))) return false;
    TArray<FBridgeBlock> B;TArray<uint16> W;TArray<uint8> S;
    for(const auto& Value:*Rows) {if(!Value.IsValid()||Value->Type!=EJson::Object) return false;auto R=Value->AsObject();FBridgeBlock Block;FVector Source;int Role,SkyLight,BlockLight,Emission,Opacity;
        if(!Vector(R,TEXT("p"),Block.Position)||!Vector(R,TEXT("s"),Block.Size)||!Vector(R,TEXT("source"),Source)||Source!=FVector(FIntVector(Source))||!R->TryGetStringField(TEXT("id"),Block.BlockId)||Block.BlockId.Len()>512||!R->TryGetStringField(TEXT("state"),Block.StateKey)||Block.StateKey.Len()>4096||!Int(R,TEXT("tint"),0,0xffffff,Block.Color)||!Int(R,TEXT("role"),0,3,Role)||!Int(R,TEXT("sky"),0,15,SkyLight)||!Int(R,TEXT("block"),0,15,BlockLight)||!Int(R,TEXT("emission"),0,15,Emission)||!Int(R,TEXT("opacity"),0,15,Opacity)||!R->TryGetBoolField(TEXT("collision"),Block.Collision)||!R->TryGetBoolField(TEXT("light"),Block.HasLight)||!R->TryGetBoolField(TEXT("compact"),Block.NativeCompact)) return false;
        Block.SourceBlock=FIntVector(Source);Block.HasSourceBlock=true;Block.Role=uint8(Role);Block.SkyLight=uint8(SkyLight);Block.BlockLight=uint8(BlockLight);Block.Emission=uint8(Emission);Block.Opacity=uint8(Opacity);B.Add(MoveTemp(Block));}
    auto Numbers=[](const TArray<TSharedPtr<FJsonValue>>& Values,int Max,auto& Out){for(const auto& Value:Values){double N;if(!Value->TryGetNumber(N)||!FMath::IsFinite(N)||N<0||N>Max||N!=FMath::FloorToDouble(N)) return false;Out.Add(int(N));}return true;};
    if(!Numbers(*Water,511,W)||!Numbers(*Sky,15,S)) return false;return World->ApplyReplayCell(FIntVector(Key),B,W,S);
}
}
UBridgeCinematicCapture::UBridgeCinematicCapture() {PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bTickEvenWhenPaused=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;}
UBridgeCinematicCapture::~UBridgeCinematicCapture() = default;
void UBridgeCinematicCapture::Fail(const FString& Reason) {Failed=true;Status=Reason;UE_LOG(LogTemp,Error,TEXT("Bridge cinematic: %s"),*Reason);if(Offline) FinishRender(false);}
FString UBridgeCinematicCapture::ConfigureReplayFromCommandLine() {
    FString Manifest;if(!FParse::Value(FCommandLine::Get(),TEXT("BridgeNativeReplay="),Manifest)) return FString();Offline=true;
    Manifest=FPaths::ConvertRelativePathToFull(Manifest);Directory=FPaths::GetPath(Manifest);FString Text;TSharedPtr<FJsonObject> O;double Version,Frames,Seconds;
    if(IFileManager::Get().FileSize(*Manifest)>1024*1024||!FFileHelper::LoadFileToString(Text,*Manifest)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O)||!O.IsValid()||!O->TryGetNumberField(TEXT("version"),Version)||Version!=1||!O->TryGetStringField(TEXT("package"),ExpectedPackage)||!O->TryGetNumberField(TEXT("frames"),Frames)||Frames<2||Frames>3000||Frames!=FMath::FloorToDouble(Frames)||!O->TryGetNumberField(TEXT("seconds"),Seconds)||!FMath::IsFinite(Seconds)||Seconds<=0||Seconds>120) {Fail(TEXT("Invalid cinematic manifest"));return FString();}
    // Paths are fixed filenames in this folder, not executable paths from JSON.
    const FString Baseline=FPaths::Combine(Directory,TEXT("baseline.ndjson")),FramesFile=FPaths::Combine(Directory,TEXT("frames.ubrf"));FString BaselineHash,FramesHash;
    auto& Platform=FPlatformFileManager::Get().GetPlatformFile();if(Platform.FileSize(*Baseline)<=0||Platform.FileSize(*Baseline)>MaxTapeBytes||Platform.FileSize(*FramesFile)<=4||Platform.FileSize(*FramesFile)>MaxTapeBytes) {Fail(TEXT("Cinematic file size budget exceeded"));return FString();}
    if(!O->TryGetStringField(TEXT("baselineMd5"),BaselineHash)||!O->TryGetStringField(TEXT("framesMd5"),FramesHash)||LexToString(FMD5Hash::HashFile(*Baseline))!=BaselineHash||LexToString(FMD5Hash::HashFile(*FramesFile))!=FramesHash) {Fail(TEXT("Cinematic files changed or are missing; live saves were not touched"));return FString();}
    Stream.Reset(Platform.OpenRead(*FramesFile));uint8 Magic[4];if(!Stream||!Stream->Read(Magic,4)||FMemory::Memcmp(Magic,"UBR1",4)!=0) {Fail(TEXT("Invalid cinematic stream"));return FString();}
    ExpectedFrames=int32(Frames);Duration=Seconds;TotalRenderFrames=FMath::CeilToInt(Duration*60);Status=TEXT("Loading replay baseline (live saves are read-only)");return Baseline;
}
void UBridgeCinematicCapture::Initialize(ABridgeReceiver* InReceiver,ABridgeRealisticWorld* InPhysical,ABridgeWorld* InTerrain,const FString& InSave,const FString& InPackage) {
    Receiver=InReceiver;Physical=InPhysical;Terrain=InTerrain;SaveFile=InSave;Package=InPackage;
    AddTickPrerequisiteActor(Physical);AddTickPrerequisiteActor(Receiver);
    if(!Offline) return;
    Physical->ReplayActive=true;
    if(Terrain->RebuildPending()>0) {Status=TEXT("Preparing baseline terrain/lightmap before rendering");return;}
    if(Package!=ExpectedPackage) {Fail(TEXT("Replay/export package mismatch; import the export used for this recording"));return;}
    for(TActorIterator<AActor> It(GetWorld());It;++It) if(Observed(*It)) {
        if(It->IsA<ABridgeDroppedItem>()) It->SetActorHiddenInGame(true);
        else ReplayActors.Add(ActorKey(*It),*It);
    }
    UGameplayStatics::SetGamePaused(this,true);
    PreviousFixedTime=FApp::UseFixedTimeStep();PreviousFixedDelta=FApp::GetFixedDeltaTime();FixedTimeSaved=true;FApp::SetUseFixedTimeStep(true);FApp::SetFixedDeltaTime(1./60);
    if(!ReadObservation(Current)||!ReadObservation(Next)||!ActivateCurrent()) {Fail(TEXT("Cannot read replay observations"));return;}
    auto* Viewport=GetWorld()->GetGameViewport();if(!Viewport||!Viewport->Viewport) {Fail(TEXT("Replay requires a game viewport; do not use -NullRHI"));return;}
    const FIntPoint Size=Viewport->Viewport->GetSizeXY();Width=Size.X;Height=Size.Y;
    VideoFile.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*FPaths::Combine(Directory,TEXT("video-60fps.avi.part"))));
    Avi.Write=[this](const uint8* B,std::size_t N){return VideoFile&&VideoFile->Write(B,int64(N));};Avi.Seek=[this](uint64 P){return VideoFile&&VideoFile->Seek(int64(P));};
    if(!VideoFile||!Avi.begin(Width,Height)) {Fail(TEXT("Cannot create AVI (supported viewport: up to 3840x2160)"));return;}
    EndFrameHandle=FCoreDelegates::OnEndFrame.AddUObject(this,&UBridgeCinematicCapture::CaptureEndFrame);Status=TEXT("Rendering fixed 60fps AVI (silent)");
}
FString UBridgeCinematicCapture::Command(const TArray<FString>& Args) {
    if(Args.Num()!=2) return TEXT("/record start|stop|status");
    if(Args[1]==TEXT("status")) return Status;
    if(Args[1]==TEXT("stop")) {if(!Recording) return TEXT("Not recording");StopRecording();return Status;}
    if(Args[1]!=TEXT("start")) return TEXT("/record start|stop|status");
    if(Recording||Offline) return TEXT("Already recording/rendering");
    Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Cinematics"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    auto& P=FPlatformFileManager::Get().GetPlatformFile();if(!P.CreateDirectoryTree(*Directory)||!P.CopyFile(*FPaths::Combine(Directory,TEXT("baseline.ndjson")),*SaveFile)) return TEXT("Recording failed: immutable baseline copy unavailable");
    Stream.Reset(P.OpenWrite(*FPaths::Combine(Directory,TEXT("frames.ubrf.part"))));if(!Stream||!Stream->Write(reinterpret_cast<const uint8*>("UBR1"),4)) {Stream.Reset();return TEXT("Recording failed: stream unavailable");}
    FrameCount=0;WrittenBytes=4;Elapsed=0;RecordClock=0;DirtyCells.Reset();PendingExplosions.Reset();Recording=true;Failed=false;
    const TWeakObjectPtr<UBridgeCinematicCapture> WeakThis(this);Terrain->NativeCellChanged=[WeakThis](const FIntVector& Cell){if(WeakThis.IsValid()&&WeakThis->Recording) WeakThis->DirtyCells.Add(Cell);};
    if(!WriteObservation()) {Recording=false;Terrain->NativeCellChanged=nullptr;Stream.Reset();return TEXT("Recording failed; baseline retained for inspection");}
    Status=TEXT("Recording (120 seconds / 512 MiB limit). /record stop");return Status;
}
void UBridgeCinematicCapture::RecordExplosion(const FVector& Position,bool Realistic,int32 Quality) {
    if(!Recording) return;auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("p"),V(Position));O->SetBoolField(TEXT("realistic"),Realistic);O->SetNumberField(TEXT("quality"),Quality);PendingExplosions.Add(MakeShared<FJsonValueObject>(O));
}
bool UBridgeCinematicCapture::WriteObservation() {
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("time"),Elapsed);O->SetObjectField(TEXT("physics"),Physical->ExportState());
    auto* PC=Cast<ABridgeNativePlayerController>(UGameplayStatics::GetPlayerController(this,0));if(!PC||!PC->PlayerCameraManager) return false;
    O->SetArrayField(TEXT("camera"),V(PC->PlayerCameraManager->GetCameraLocation()));const auto R=PC->PlayerCameraManager->GetCameraRotation();O->SetArrayField(TEXT("aim"),V(FVector(R.Pitch,R.Yaw,R.Roll)));O->SetNumberField(TEXT("fov"),PC->PlayerCameraManager->GetFOVAngle());
    O->SetNumberField(TEXT("health"),Receiver->GetNativeHealth());O->SetNumberField(TEXT("timeOfDay"),Receiver->NativeTimeOfDay);O->SetNumberField(TEXT("worldTime"),Receiver->NativeWorldTime);O->SetNumberField(TEXT("perspective"),Receiver->LatestInput.Perspective);
    if(auto* Inventory=PC->GetNativeInventory()) O->SetObjectField(TEXT("inventory"),Inventory->ExportRuntimeState());
    TArray<TSharedPtr<FJsonValue>> Actors,Cells;
    for(TActorIterator<AActor> It(GetWorld());It;++It) if(Observed(*It)) {
        auto A=MakeShared<FJsonObject>();A->SetStringField(TEXT("key"),ActorKey(*It));A->SetObjectField(TEXT("transform"),Transform(It->GetActorTransform()));A->SetBoolField(TEXT("hidden"),It->IsHidden());
        if(auto* D=Cast<ABridgeDroppedItem>(*It)) {A->SetStringField(TEXT("model"),D->GetModelKey());A->SetNumberField(TEXT("count"),D->GetQuantity());}
        if(auto* M=Cast<ABridgeMobCharacter>(*It)) {const auto S=M->NativeSnapshot(Receiver->Anchor,Receiver->SourceOrigin);A->SetStringField(TEXT("mobType"),S.Type);A->SetStringField(TEXT("appearance"),S.Appearance);A->SetNumberField(TEXT("health"),S.Health);}
        auto C=MakeShared<FJsonObject>();TInlineComponentArray<USceneComponent*> Components;It->GetComponents(Components);
        for(auto* Component:Components) if(Component!=It->GetRootComponent()) {auto T=Transform(Component->GetRelativeTransform());T->SetBoolField(TEXT("visible"),Component->IsVisible());C->SetObjectField(Component->GetName(),T);}
        A->SetObjectField(TEXT("components"),C);Actors.Add(MakeShared<FJsonValueObject>(A));
        if(It->IsA<ABridgeCharacter>()) A->SetObjectField(TEXT("meshes"),CapturePlayerMeshes(*It));
    }
    for(const auto& Cell:DirtyCells) if(auto Snapshot=CellSnapshot(Terrain,Cell)) Cells.Add(MakeShared<FJsonValueObject>(Snapshot));
    O->SetArrayField(TEXT("actors"),Actors);O->SetArrayField(TEXT("cells"),Cells);O->SetArrayField(TEXT("explosions"),PendingExplosions);
    const FString Text=Json(O);FTCHARToUTF8 Utf8(*Text);int32 Raw=Utf8.Length();if(Raw<=0||Raw>MaxObservationBytes) return false;
    int32 Compressed=FCompression::CompressMemoryBound(NAME_Zlib,Raw);TArray<uint8> Data;Data.SetNumUninitialized(Compressed);
    if(!FCompression::CompressMemory(NAME_Zlib,Data.GetData(),Compressed,Utf8.Get(),Raw)) return false;
    if(WrittenBytes+Compressed+8>MaxTapeBytes) return false;
    std::vector<uint8> Prefix;BridgeAvi::U32(Prefix,uint32(Raw));BridgeAvi::U32(Prefix,uint32(Compressed));
    if(!Stream->Write(Prefix.data(),8)||!Stream->Write(Data.GetData(),Compressed)) return false;
    ++FrameCount;LastWrittenTime=Elapsed;WrittenBytes+=8+Compressed;DirtyCells.Reset();PendingExplosions.Reset();return true;
}
bool UBridgeCinematicCapture::StopRecording() {
    if(!Recording) return false;Recording=false;Terrain->NativeCellChanged=nullptr;Stream.Reset();
    if(FrameCount<2||LastWrittenTime<=0) {Status=TEXT("Recording too short (minimum two observations); incomplete files retained, no replay published");return false;}
    const FString FramesFile=FPaths::Combine(Directory,TEXT("frames.ubrf"));
    if(!BridgeNativeFile::Replace(FramesFile,FramesFile+TEXT(".part"))) {Status=TEXT("Could not finalize recording stream; incomplete files retained");return false;}
    auto M=MakeShared<FJsonObject>();M->SetNumberField(TEXT("version"),1);M->SetStringField(TEXT("package"),Package);M->SetNumberField(TEXT("frames"),FrameCount);M->SetNumberField(TEXT("seconds"),LastWrittenTime);M->SetNumberField(TEXT("renderFps"),60);M->SetBoolField(TEXT("audio"),false);M->SetStringField(TEXT("baselineMd5"),LexToString(FMD5Hash::HashFile(*FPaths::Combine(Directory,TEXT("baseline.ndjson")))));M->SetStringField(TEXT("framesMd5"),LexToString(FMD5Hash::HashFile(*FramesFile)));
    const FString Manifest=FPaths::Combine(Directory,TEXT("replay.json"));
    if(!FFileHelper::SaveStringToFile(Json(M),*(Manifest+TEXT(".part")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!BridgeNativeFile::Replace(Manifest,Manifest+TEXT(".part"))) {Status=TEXT("Could not finalize replay manifest");return false;}
    Status=TEXT("Recording saved: ")+Manifest+TEXT(". Close the game and run Render-Replay.cmd.");UE_LOG(LogTemp,Display,TEXT("Bridge cinematic: %s"),*Status);return true;
}
bool UBridgeCinematicCapture::ReadObservation(TSharedPtr<FJsonObject>& Out) {
    if(!Stream||LoadedFrames>=ExpectedFrames||Stream->Tell()+8>Stream->Size()) return false;
    uint8 Prefix[8];if(!Stream->Read(Prefix,8)) return false;
    auto Read32=[&](int Offset){uint32 N=0;for(int I=0;I<4;++I) N|=uint32(Prefix[Offset+I])<<(I*8);return N;};
    const uint32 Raw=Read32(0),Compressed=Read32(4);if(Raw<1||Raw>MaxObservationBytes||Compressed<1||Compressed>MaxObservationBytes||Stream->Tell()+Compressed>Stream->Size()) return false;
    TArray<uint8> Data,Plain;Data.SetNumUninitialized(Compressed);Plain.SetNumZeroed(Raw+1);
    if(!Stream->Read(Data.GetData(),Compressed)||!FCompression::UncompressMemory(NAME_Zlib,Plain.GetData(),Raw,Data.GetData(),Compressed)) return false;
    const FString Text=FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Plain.GetData()),Raw).Get());double Time;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)||!Out.IsValid()||!Out->TryGetNumberField(TEXT("time"),Time)||!FMath::IsFinite(Time)||Time<=LastTime||Time<0||Time>Duration+.001) return false;
    LastTime=Time;++LoadedFrames;
    if(LoadedFrames==ExpectedFrames&&(Stream->Tell()!=Stream->Size()||FMath::Abs(Time-Duration)>.001)) return false;
    return true;
}
bool UBridgeCinematicCapture::ActivateCurrent() {
    const TArray<TSharedPtr<FJsonValue>> *Cells=nullptr,*Effects=nullptr;const TSharedPtr<FJsonObject>* P=nullptr;
    if(!Current->TryGetObjectField(TEXT("physics"),P)||!Physical->ImportState(*P)||!Current->TryGetArrayField(TEXT("cells"),Cells)||Cells->Num()>8192||!Current->TryGetArrayField(TEXT("explosions"),Effects)||Effects->Num()>64) return false;
    CurrentPhysics=Physical->Physics;
    const bool CurrentVisuals=Physical->VisualsEnabled();const int32 CurrentQuality=Physical->GetQuality();
    if(Next.IsValid()) {if(!Next->TryGetObjectField(TEXT("physics"),P)||!Physical->ImportState(*P)) return false;NextPhysics=Physical->Physics;Physical->Physics=CurrentPhysics;}
    else NextPhysics=CurrentPhysics;
    if(Physical->VisualsEnabled()!=CurrentVisuals||Physical->GetQuality()!=CurrentQuality) Physical->SetVisuals(CurrentVisuals,CurrentQuality);
    Receiver->Video->SetNativeRealisticMode(CurrentVisuals);
    for(const auto& Cell:*Cells) if(!Cell.IsValid()||Cell->Type!=EJson::Object||!ApplyCell(Terrain,Cell->AsObject())) return false;
    if(!Terrain->FlushReplayUpdates()) return false;
    for(const auto& Effect:*Effects) {if(!Effect.IsValid()||Effect->Type!=EJson::Object) return false;auto E=Effect->AsObject();FVector Position;bool Lit;double Quality;
        if(!Vector(E,TEXT("p"),Position)||!E->TryGetBoolField(TEXT("realistic"),Lit)||!E->TryGetNumberField(TEXT("quality"),Quality)||Quality<0||Quality>2) return false;
        auto* FX=ABridgeRealisticExplosion::Spawn(GetWorld(),Position,Lit,int32(Quality));if(FX) {FX->SetActorTickEnabled(false);FX->Tags.Add(FName(*FString::Printf(TEXT("ReplayStart=%.9f"),Current->GetNumberField(TEXT("time")))));ReplayEffects.Add(FX);}}
    if(Current->TryGetObjectField(TEXT("inventory"),P)) {auto* PC=Cast<ABridgeNativePlayerController>(UGameplayStatics::GetPlayerController(this,0));if(!PC||!PC->GetNativeInventory()->ImportRuntimeState(*P)) return false;Receiver->NativeSelect(PC->GetNativeInventory()->GetSelectedItemId());}
    double Value;if(Current->TryGetNumberField(TEXT("health"),Value)&&Receiver->MobWorld) Receiver->MobWorld->PlayerHealth=float(Value);
    if(Current->TryGetNumberField(TEXT("timeOfDay"),Value)) Receiver->NativeTimeOfDay=Value;if(Current->TryGetNumberField(TEXT("worldTime"),Value)) Receiver->NativeWorldTime=Value;Receiver->TickNativeTime(0);
    return true;
}
void UBridgeCinematicCapture::RenderPose(double Time) {
    const double A=Current->GetNumberField(TEXT("time")),B=Next.IsValid()?Next->GetNumberField(TEXT("time")):A;const float Alpha=B>A?float(FMath::Clamp((Time-A)/(B-A),0.,1.)):0;
    Physical->Physics=CurrentPhysics;
    auto Lerp=[](BridgeRealistic::Vec P,BridgeRealistic::Vec Q,double T){return P+(Q-P)*T;};
    if(CurrentPhysics.grains.size()==NextPhysics.grains.size()) for(std::size_t I=0;I<Physical->Physics.grains.size();++I) Physical->Physics.grains[I].p=Lerp(CurrentPhysics.grains[I].p,NextPhysics.grains[I].p,Alpha);
    for(auto& Bomb:Physical->Physics.bombs) for(const auto& N:NextPhysics.bombs) if(Bomb.id==N.id) Bomb.p=Lerp(Bomb.p,N.p,Alpha);
    if(CurrentPhysics.drops.size()==NextPhysics.drops.size()) for(std::size_t I=0;I<Physical->Physics.drops.size();++I) Physical->Physics.drops[I].p=Lerp(CurrentPhysics.drops[I].p,NextPhysics.drops[I].p,Alpha);
    for(const auto& N:NextPhysics.liquids) if(!Physical->Physics.liquids.count(N.first)) Physical->Physics.liquids.emplace(N.first,BridgeRealistic::Liquid{N.second.kind,0});
    for(auto& L:Physical->Physics.liquids) {auto N=NextPhysics.liquids.find(L.first);int Target=N==NextPhysics.liquids.end()?0:N->second.amount;L.second.amount=FMath::RoundToInt(FMath::Lerp(float(L.second.amount),float(Target),Alpha));}
    Physical->RenderNow(CurrentPhysics.tick/60.+(Time-A));
    FVector Eye,Aim,OtherEye,OtherAim;if(!Vector(Current,TEXT("camera"),Eye)||!Vector(Current,TEXT("aim"),Aim)) {Fail(TEXT("Invalid replay camera"));return;}
    if(Next.IsValid()&&Vector(Next,TEXT("camera"),OtherEye)&&Vector(Next,TEXT("aim"),OtherAim)) {Eye=FMath::Lerp(Eye,OtherEye,Alpha);const FQuat Q=FQuat::Slerp(FRotator(Aim.X,Aim.Y,Aim.Z).Quaternion(),FRotator(OtherAim.X,OtherAim.Y,OtherAim.Z).Quaternion(),Alpha);const auto R=Q.Rotator();Aim=FVector(R.Pitch,R.Yaw,R.Roll);}
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    const TArray<TSharedPtr<FJsonValue>> *Actors=nullptr,*NextActors=nullptr;if(!Current->TryGetArrayField(TEXT("actors"),Actors)||Actors->Num()>1024) {Fail(TEXT("Replay actor budget exceeded"));return;}
    TMap<FString,TSharedPtr<FJsonObject>> Upcoming;if(Next.IsValid()&&Next->TryGetArrayField(TEXT("actors"),NextActors)) for(const auto& Value:*NextActors) {if(Value.IsValid()&&Value->Type==EJson::Object) {FString Key;if(Value->AsObject()->TryGetStringField(TEXT("key"),Key)) Upcoming.Add(Key,Value->AsObject());}}
    for(const auto& Pair:ReplayActors) if(IsValid(Pair.Value)) Pair.Value->SetActorHiddenInGame(true);
    for(const auto& Value:*Actors) {
        if(!Value.IsValid()||Value->Type!=EJson::Object) {Fail(TEXT("Invalid actor observation"));return;}auto O=Value->AsObject();FString Key;const TSharedPtr<FJsonObject>* Tr=nullptr;FTransform T;
        if(!O->TryGetStringField(TEXT("key"),Key)||!O->TryGetObjectField(TEXT("transform"),Tr)||!ReadTransform(*Tr,T)) {Fail(TEXT("Invalid actor transform"));return;}
        auto& Actor=ReplayActors.FindOrAdd(Key);
        if(!IsValid(Actor)) {
            FString Model,Type,Appearance;double Count;
            if(O->TryGetStringField(TEXT("model"),Model)&&O->TryGetNumberField(TEXT("count"),Count)&&Count>=1&&Count<=99) {auto* Drop=GetWorld()->SpawnActor<ABridgeDroppedItem>();if(Drop&&Drop->Initialize(Receiver->TexturePalette,Model,int32(Count),FVector::ZeroVector)) Actor=Drop;else if(Drop) Drop->Destroy();}
            else if(O->TryGetStringField(TEXT("mobType"),Type)&&O->TryGetStringField(TEXT("appearance"),Appearance)&&Receiver->MobPalette) {
                if(const auto* App=Receiver->MobPalette->Find(Appearance)) {FBridgeMobSnapshot S;S.Id=Key.Mid(4);S.Type=Type;S.Appearance=Appearance;S.Position=Receiver->SourceOrigin;S.Width=App->Width;S.Height=App->Height;S.MaxHealth=App->MaxHealth;S.Health=App->MaxHealth;
                    auto* Mob=GetWorld()->SpawnActor<ABridgeMobCharacter>();if(Mob&&Mob->Initialize(S,*App,Receiver->MobWorld)) Actor=Mob;else if(Mob) Mob->Destroy();}}
            if(!IsValid(Actor)) {Fail(TEXT("Replay actor cannot be recreated: ")+Key);return;}
        }
        Actor->SetActorTickEnabled(false);bool Hidden=false;O->TryGetBoolField(TEXT("hidden"),Hidden);Actor->SetActorHiddenInGame(Hidden);
        if(auto* Drop=Cast<ABridgeDroppedItem>(Actor)) {double Count;if(O->TryGetNumberField(TEXT("count"),Count)&&Count>=1&&Count<=99&&Count==FMath::FloorToDouble(Count)) Drop->SetQuantity(int32(Count));}
        const auto* NextO=Upcoming.Find(Key);if(NextO&&(*NextO)->TryGetObjectField(TEXT("transform"),Tr)) {FTransform Other;if(ReadTransform(*Tr,Other)) {FTransform From=T;T.Blend(From,Other,Alpha);}}
        Actor->SetActorTransform(T,false,nullptr,ETeleportType::TeleportPhysics);
        if(O->TryGetObjectField(TEXT("components"),Tr)) {
            TInlineComponentArray<USceneComponent*> Parts;Actor->GetComponents(Parts);
            for(auto* C:Parts) {const TSharedPtr<FJsonObject>* Recorded=nullptr;if((*Tr)->TryGetObjectField(C->GetName(),Recorded)) {FTransform Relative;if(ReadTransform(*Recorded,Relative)) {
                const TSharedPtr<FJsonObject> *NextParts=nullptr,*NextPart=nullptr;
                if(NextO&&(*NextO)->TryGetObjectField(TEXT("components"),NextParts)&&(*NextParts)->TryGetObjectField(C->GetName(),NextPart)) {FTransform Other;if(ReadTransform(*NextPart,Other)) {FTransform From=Relative;Relative.Blend(From,Other,Alpha);}}
                bool Visible=true;(*Recorded)->TryGetBoolField(TEXT("visible"),Visible);C->SetVisibility(Visible);C->SetRelativeTransform(Relative);}}}
        }
        if(Actor->IsA<ABridgeCharacter>()) {
            const TSharedPtr<FJsonObject>* Meshes=nullptr;TSharedPtr<FJsonObject> Future;
            if(NextO&&(*NextO)->TryGetObjectField(TEXT("meshes"),Tr)) Future=*Tr;
            if(!O->TryGetObjectField(TEXT("meshes"),Meshes)||!ApplyPlayerMeshes(Actor,*Meshes,Future,Alpha)) {Fail(TEXT("Invalid recorded player/held-item geometry"));return;}
        }
    }
    for(auto& FX:ReplayEffects) if(IsValid(FX)) for(const auto& Tag:FX->Tags) {const FString S=Tag.ToString();if(S.StartsWith(TEXT("ReplayStart="))) {const float Age=float(Time-FCString::Atod(*S.Mid(12)));FX->SetActorHiddenInGame(Age>5);if(Age<=5) FX->ShowAge(Age);}}
    // Keep the pawn's existing postprocess camera: a new CameraActor would lose
    // the inverse crosshair and independent hand-depth blend.
    auto* Character=Cast<ABridgeCharacter>(Receiver->TargetCharacter);
    if(!PC||!Character||!Character->BridgeCamera) {Fail(TEXT("Replay player camera unavailable"));return;}
    PC->SetViewTarget(Character);double Fov=80,Perspective=0;Current->TryGetNumberField(TEXT("fov"),Fov);Current->TryGetNumberField(TEXT("perspective"),Perspective);
    double FutureFov;if(Next.IsValid()&&Next->TryGetNumberField(TEXT("fov"),FutureFov)) Fov=FMath::Lerp(Fov,FutureFov,double(Alpha));
    double Clock,FutureClock;if(Current->TryGetNumberField(TEXT("worldTime"),Clock)) {
        if(Next.IsValid()&&Next->TryGetNumberField(TEXT("worldTime"),FutureClock)&&FutureClock>=Clock&&FutureClock-Clock<10) Clock=FMath::Lerp(Clock,FutureClock,double(Alpha));
        Receiver->NativeWorldTime=Clock;Receiver->NativeTimeOfDay=FMath::Fmod(Clock,24000.);Receiver->TickNativeTime(0);
    }
    Character->SetReplayPerspective(int32(Perspective));Character->BridgeCamera->SetWorldLocationAndRotation(Eye,FRotator(Aim.X,Aim.Y,Aim.Z));Character->BridgeCamera->SetFieldOfView(float(Fov));
    Character->BridgeCamera->bUsePawnControlRotation=false;
    PC->SetControlRotation(FRotator(Aim.X,Aim.Y,Aim.Z));PC->PlayerCameraManager->UpdateCamera(0);
    Receiver->Video->RefreshNativeReplayView();
}
void UBridgeCinematicCapture::TickComponent(float DeltaSeconds,ELevelTick Type,FActorComponentTickFunction* Tick) {
    Super::TickComponent(DeltaSeconds,Type,Tick);
    if(Offline&&!Failed&&Receiver&&!Current.IsValid()&&Terrain&&Terrain->RebuildPending()==0&&Receiver->IsNativeReady()) Initialize(Receiver,Physical,Terrain,SaveFile,Package);
    if(Recording&&!GetWorld()->IsPaused()) {Elapsed+=DeltaSeconds;RecordClock+=DeltaSeconds;
        if(RecordClock>=.05) {RecordClock=0;if(Elapsed>120) Elapsed=120;if(!WriteObservation()) {StopRecording();Status+=TEXT(" (recording stopped at budget/write limit)");}else if(Elapsed>=120) StopRecording();}}
    if(!Offline||Failed||!Receiver||!Current.IsValid()||Prepared) return;
    const double Time=RenderFrame/60.;
    while(Next.IsValid()&&Time>=Next->GetNumberField(TEXT("time"))) {Current=Next;Next.Reset();if(LoadedFrames<ExpectedFrames&&!ReadObservation(Next)) {Fail(TEXT("Truncated/invalid replay stream"));return;}if(!ActivateCurrent()) {Fail(TEXT("Invalid replay state"));return;}}
    RenderPose(Time);if(!Failed) Prepared=true;
}
void UBridgeCinematicCapture::CaptureEndFrame() {
    if(!Prepared||Failed||!Offline||!VideoFile) return;
    auto* Viewport=GetWorld()->GetGameViewport();if(!Viewport||!Viewport->Viewport) {Fail(TEXT("Replay viewport closed"));return;}
    if(Viewport->Viewport->GetSizeXY()!=FIntPoint(Width,Height)) {Fail(TEXT("Window size changed during rendering; keep the render window size fixed"));return;}
    FlushRenderingCommands();TArray<FColor> Pixels;
    if(!Viewport->Viewport->ReadPixels(Pixels)||Pixels.Num()!=Width*Height) {Fail(TEXT("GPU readback failed"));return;}
    auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Encoder=Module.CreateImageWrapper(EImageFormat::JPEG);
    if(!Encoder||!Encoder->SetRaw(Pixels.GetData(),Pixels.Num()*sizeof(FColor),Width,Height,ERGBFormat::BGRA,8)) {Fail(TEXT("JPEG encoding failed"));return;}
    const auto& Bytes=Encoder->GetCompressed(95);
    if(!Avi.append(Bytes.GetData(),std::size_t(Bytes.Num()))) {Fail(TEXT("AVI write/2 GiB budget limit reached; incomplete .part retained"));return;}
    ++RenderFrame;Prepared=false;
    if(RenderFrame>=TotalRenderFrames) FinishRender(true);
}
void UBridgeCinematicCapture::FinishRender(bool Success) {
    FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);EndFrameHandle.Reset();
    if(FixedTimeSaved) {FApp::SetUseFixedTimeStep(PreviousFixedTime);FApp::SetFixedDeltaTime(PreviousFixedDelta);FixedTimeSaved=false;}
    if(!Offline) return;
    const bool Complete=Success&&VideoFile&&Avi.finish();VideoFile.Reset();Stream.Reset();
    const FString Output=FPaths::Combine(Directory,TEXT("video-60fps.avi"));
    if(Complete&&BridgeNativeFile::Replace(Output,Output+TEXT(".part"))) {Status=TEXT("60fps render complete: ")+Output;UE_LOG(LogTemp,Display,TEXT("Bridge cinematic COMPLETE: %s frames=%d fps=60 audio=none"),*Output,RenderFrame);FPlatformMisc::RequestExitWithStatus(false,0);}
    else {UE_LOG(LogTemp,Error,TEXT("Bridge cinematic FAILED: %s; live saves untouched, incomplete AVI retained"),*Status);FPlatformMisc::RequestExitWithStatus(false,1);}
}
void UBridgeCinematicCapture::EndPlay(const EEndPlayReason::Type Reason) {
    if(Recording) StopRecording();FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
    if(FixedTimeSaved) {FApp::SetUseFixedTimeStep(PreviousFixedTime);FApp::SetFixedDeltaTime(PreviousFixedDelta);FixedTimeSaved=false;}
    Super::EndPlay(Reason);
}
