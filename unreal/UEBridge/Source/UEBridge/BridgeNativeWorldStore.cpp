#include "BridgeNativeWorldStore.h"
#include "BridgeBiomeTint.h"
#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "BridgeLightingService.h"
#include "BridgeNativeFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace {
constexpr int64 MaxFileBytes=512ll*1024*1024;
constexpr int32 MaxLineBytes=2*1024*1024;
constexpr int64 MaxRows=2097152;
bool Number(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double Low,double High,double& Value) {
    return Object.IsValid() && Object->TryGetNumberField(Key,Value) && FMath::IsFinite(Value) && Value>=Low && Value<=High;
}
bool Integer(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,int32 Low,int32 High,int32& Value) {
    double N;if(!Number(Object,Key,Low,High,N) || N!=FMath::FloorToDouble(N)) return false;Value=int32(N);return true;
}
bool ArrayNumber(const TArray<TSharedPtr<FJsonValue>>& Array,int32 Index,double Low,double High,double& Value) {
    return Array.IsValidIndex(Index) && Array[Index].IsValid() && Array[Index]->TryGetNumber(Value) && FMath::IsFinite(Value) && Value>=Low && Value<=High;
}
bool ArrayInt(const TArray<TSharedPtr<FJsonValue>>& Array,int32 Index,int32 Low,int32 High,int32& Value) {
    double N;if(!ArrayNumber(Array,Index,Low,High,N) || N!=FMath::FloorToDouble(N)) return false;Value=int32(N);return true;
}
bool VectorArray(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,FVector& Out,double Limit) {
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;double X,Y,Z;
    if(!Object->TryGetArrayField(Key,A) || A->Num()!=3 || !ArrayNumber(*A,0,-Limit,Limit,X) || !ArrayNumber(*A,1,-Limit,Limit,Y) || !ArrayNumber(*A,2,-Limit,Limit,Z)) return false;
    Out=FVector(X,Y,Z);return true;
}
bool Identifier(const FString& Id) {
    if(Id.IsEmpty() || Id.Len()>256) return false;
    int32 Colons=0;for(TCHAR C:Id) {
        if(C==':') {++Colons;continue;}
        if(!((C>='a'&&C<='z') || (C>='0'&&C<='9') || C=='_' || C=='-' || C=='.' || C=='/')) return false;
    }
    return Colons==1 && !Id.StartsWith(TEXT(":")) && !Id.EndsWith(TEXT(":")) && !Id.Contains(TEXT(".."));
}
bool StateKey(const FString& Key) {
    if(Key.Len()>1024) return false;
    for(TCHAR C:Key) if(!((C>='a'&&C<='z') || (C>='0'&&C<='9') || C=='_' || C=='=' || C==',' || C=='.' || C=='-')) return false;
    return true;
}
bool ParseJson(const FString& Text,TSharedPtr<FJsonObject>& Out) {
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out) && Out.IsValid();
}
TArray<TSharedPtr<FJsonValue>> Vec(const FVector& V) {
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
TSharedPtr<FJsonObject> Header(const FBridgeNativeWorldMetadata& M) {
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("type"),TEXT("native_world"));O->SetNumberField(TEXT("schema"),1);
    O->SetStringField(TEXT("id"),M.PackageId);O->SetStringField(TEXT("dimension"),M.Dimension);
    O->SetArrayField(TEXT("origin"),Vec(M.Origin));O->SetArrayField(TEXT("spawn"),Vec(M.Spawn));
    O->SetNumberField(TEXT("yaw"),M.Yaw);O->SetNumberField(TEXT("pitch"),M.Pitch);
    O->SetArrayField(TEXT("center"),Vec(FVector(M.Center)));O->SetNumberField(TEXT("radius"),M.Radius);
    O->SetNumberField(TEXT("halfHeight"),M.HalfHeight);O->SetNumberField(TEXT("cells"),M.Cells);
    if(M.Environment) O->SetObjectField(TEXT("vanillaLight"),M.Environment);
    if(M.RuntimeState) O->SetObjectField(TEXT("runtimeState"),M.RuntimeState);
    O->SetArrayField(TEXT("mobs"),M.Mobs);
    return O;
}
}

FBridgeNativeWorldStore::FBridgeNativeWorldStore() {ReadBuffer.SetNumUninitialized(65536);}
FBridgeNativeWorldStore::~FBridgeNativeWorldStore() {Cancel();}
bool FBridgeNativeWorldStore::IsLoading() const {return State==EState::Validating || State==EState::Loading;}
bool FBridgeNativeWorldStore::IsSaving() const {return State==EState::Saving;}
bool FBridgeNativeWorldStore::IsReady() const {return State==EState::Ready || State==EState::Saving;}
FString FBridgeNativeWorldStore::GetState() const {
    switch(State) {case EState::Validating:return TEXT("VALIDATING");case EState::Loading:return TEXT("LOADING");case EState::Ready:return TEXT("READY");case EState::Saving:return TEXT("SAVING");case EState::Failed:return TEXT("FAILED");default:return TEXT("IDLE");}
}
float FBridgeNativeWorldStore::GetProgress() const {
    if(State==EState::Saving) return SaveCells.IsEmpty() ? 0.f : float(SaveCursor)/SaveCells.Num();
    if(State==EState::Validating) return FileSize>0 ? .5f*float(BytesRead)/FileSize : 0.f;
    if(State==EState::Loading) return .5f+.5f*float(ProcessedCells)/FMath::Max(1,Metadata.Cells);
    return State==EState::Ready ? 1.f : 0.f;
}
void FBridgeNativeWorldStore::Cancel() {
    Reader.Reset();Writer.Reset();
    if(!TemporaryFile.IsEmpty()) FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TemporaryFile);
    TemporaryFile.Empty();SaveCells.Empty();SaveSnapshot.Empty();SeenCells.Empty();ValidatedModels.Empty();State=EState::Idle;
}
void FBridgeNativeWorldStore::Fail(const FString& Reason) {
    const bool Saving=IsSaving();Reader.Reset();Writer.Reset();
    if(!TemporaryFile.IsEmpty()) FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TemporaryFile);
    TemporaryFile.Empty();SaveSnapshot.Empty();SaveCells.Empty();Error=Reason.Left(512);State=Saving && Target.IsValid() && Target->IsSealed() ? EState::Ready : EState::Failed;
    UE_LOG(LogTemp,Error,TEXT("Bridge native world: %s"),*Error);
}
bool FBridgeNativeWorldStore::OpenReader(const FString& Path) {
    Reader.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
    if(!Reader) {Fail(TEXT("Cannot open offline world: ")+Path);return false;}
    FileSize=Reader->Size();if(FileSize<=0 || FileSize>MaxFileBytes) {Fail(TEXT("Offline world exceeds the 512 MiB file budget or is empty"));return false;}
    ReadCursor=ReadCount=Line=0;BytesRead=0;return true;
}
bool FBridgeNativeWorldStore::ReadLine(FString& Out,bool& AtEnd) {
    TArray<uint8> Bytes;Bytes.Reserve(4096);AtEnd=false;
    for(;;) {
        if(ReadCursor==ReadCount) {
            const int64 Remaining=FileSize-BytesRead;
            if(Remaining<=0) {AtEnd=Bytes.IsEmpty();break;}
            ReadCount=int32(FMath::Min<int64>(ReadBuffer.Num(),Remaining));ReadCursor=0;
            if(!Reader->Read(ReadBuffer.GetData(),ReadCount)) {Fail(TEXT("Read failed; original world/save has not been replaced"));return false;}
            BytesRead+=ReadCount;
        }
        const uint8 B=ReadBuffer[ReadCursor++];if(B=='\n') break;
        if(B==0 || Bytes.Num()>=MaxLineBytes) {Fail(TEXT("Invalid or oversized world record"));return false;}
        if(B!='\r') Bytes.Add(B);
    }
    if(AtEnd) return true;
    ++Line;FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),Bytes.Num());Out=FString(Text.Length(),Text.Get());
    if(Line==1 && Out.StartsWith(TEXT("\ufeff"))) Out.RightChopInline(1);
    if(Out.IsEmpty()) {Fail(FString::Printf(TEXT("Empty world record at line %d"),Line));return false;}
    return true;
}
bool FBridgeNativeWorldStore::ReadHeader(const FString& Text,FBridgeNativeWorldMetadata& Out) {
    TSharedPtr<FJsonObject> O;FString Type;int32 Schema;FVector C;FGuid Id;
    if(!ParseJson(Text,O) || !O->TryGetStringField(TEXT("type"),Type) || Type!=TEXT("native_world") || !Integer(O,TEXT("schema"),1,1,Schema)
        || !O->TryGetStringField(TEXT("id"),Out.PackageId) || !FGuid::ParseExact(Out.PackageId,EGuidFormats::DigitsWithHyphens,Id)
        || !O->TryGetStringField(TEXT("dimension"),Out.Dimension) || !Identifier(Out.Dimension)
        || !VectorArray(O,TEXT("origin"),Out.Origin,30000000) || !VectorArray(O,TEXT("spawn"),Out.Spawn,30000000)
        || !VectorArray(O,TEXT("center"),C,3750000) || C.X!=FMath::FloorToDouble(C.X) || C.Y!=FMath::FloorToDouble(C.Y) || C.Z!=FMath::FloorToDouble(C.Z)
        || !Integer(O,TEXT("radius"),1,12,Out.Radius) || !Integer(O,TEXT("halfHeight"),1,6,Out.HalfHeight) || !Integer(O,TEXT("cells"),27,8125,Out.Cells)
        || !Number(O,TEXT("yaw"),-1e9,1e9,Out.Yaw) || !Number(O,TEXT("pitch"),-90,90,Out.Pitch)) return false;
    Out.Center=FIntVector(int32(C.X),int32(C.Y),int32(C.Z));
    if(Out.Cells!=(2*Out.Radius+1)*(2*Out.Radius+1)*(2*Out.HalfHeight+1) || (Out.Spawn-Out.Origin).GetAbs().GetMax()>100000) return false;
    const FIntVector SpawnCell(FMath::FloorToInt(Out.Spawn.X/8),FMath::FloorToInt(Out.Spawn.Y/8),FMath::FloorToInt(Out.Spawn.Z/8));
    if(FMath::Abs(SpawnCell.X-Out.Center.X)>Out.Radius || FMath::Abs(SpawnCell.Y-Out.Center.Y)>Out.HalfHeight || FMath::Abs(SpawnCell.Z-Out.Center.Z)>Out.Radius) return false;
    const TSharedPtr<FJsonObject>* Environment=nullptr;
    if(O->TryGetObjectField(TEXT("vanillaLight"),Environment)) {
        double N;for(const TCHAR* K:{TEXT("skyFactor"),TEXT("blockFactor")}) if(!Number(*Environment,K,0,4,N)) return false;
        for(const TCHAR* K:{TEXT("ambient"),TEXT("gamma"),TEXT("nightVision"),TEXT("darkness"),TEXT("darkenWorld")}) if(!Number(*Environment,K,0,1,N)) return false;
        int32 Color;for(const TCHAR* K:{TEXT("skyColor"),TEXT("ambientColor")}) if(!Integer(*Environment,K,0,0xffffff,Color)) return false;
        if(!(*Environment)->HasTypedField<EJson::Boolean>(TEXT("hasSky"))) return false;Out.Environment=*Environment;
    } else if(O->HasField(TEXT("vanillaLight"))) return false;
    const TSharedPtr<FJsonObject>* Runtime=nullptr;
    if(O->HasField(TEXT("runtimeState"))) {
        if(!O->TryGetObjectField(TEXT("runtimeState"),Runtime) || !Runtime || !Runtime->IsValid()) return false;
        FString Encoded;FJsonSerializer::Serialize((*Runtime).ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Encoded));if(Encoded.Len()>262144) return false;
        const auto& R=*Runtime;
        // Optional fields support earlier snapshots, but a present field of the
        // wrong type is corruption, never permission to replace it with defaults.
        if(R->HasField(TEXT("inventory")) && !R->HasTypedField<EJson::Object>(TEXT("inventory"))) return false;
        for(const TCHAR* Key:{TEXT("lighting"),TEXT("flying")}) if(R->HasField(Key) && !R->HasTypedField<EJson::Boolean>(Key)) return false;
        double Health;if(R->HasField(TEXT("health")) && !Number(R,TEXT("health"),0,20,Health)) return false;
        int32 Perspective;if(R->HasField(TEXT("perspective")) && !Integer(R,TEXT("perspective"),0,2,Perspective)) return false;
        if(R->HasField(TEXT("respawn"))) {
            FVector Respawn;if(!VectorArray(R,TEXT("respawn"),Respawn,30000000) || (Respawn-Out.Origin).GetAbs().GetMax()>100000) return false;
            const FIntVector Cell(FMath::FloorToInt(Respawn.X/8),FMath::FloorToInt(Respawn.Y/8),FMath::FloorToInt(Respawn.Z/8));
            if(FMath::Abs(Cell.X-Out.Center.X)>Out.Radius || FMath::Abs(Cell.Y-Out.Center.Y)>Out.HalfHeight || FMath::Abs(Cell.Z-Out.Center.Z)>Out.Radius) return false;
        }
        for(const TCHAR* Key:{TEXT("drops"),TEXT("fuses"),TEXT("mobs")}) if(R->HasField(Key)) {
            const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
            if(!R->TryGetArrayField(Key,Values) || Values->Num()>(FString(Key)==TEXT("fuses") ? 64 : 128)) return false;
            for(const auto& Value:*Values) if(!Value.IsValid() || Value->Type!=EJson::Object) return false;
        }
        Out.RuntimeState=R;
    }
    const TArray<TSharedPtr<FJsonValue>>* Mobs=nullptr;
    if(O->HasField(TEXT("mobs"))) {
        if(!O->TryGetArrayField(TEXT("mobs"),Mobs) || Mobs->Num()>512) return false;
        TSet<FString> Seen;
        for(const auto& Value:*Mobs) {
            if(!Value.IsValid() || Value->Type!=EJson::Object) return false;
            const auto Mob=Value->AsObject();FString MobId,MobType,Appearance;FVector Position;FGuid Guid;double Yaw,Health;
            if(!Mob->TryGetStringField(TEXT("id"),MobId) || !FGuid::ParseExact(MobId,EGuidFormats::DigitsWithHyphens,Guid) || Seen.Contains(MobId)
                || !Mob->TryGetStringField(TEXT("type"),MobType) || !Identifier(MobType) || !Mob->TryGetStringField(TEXT("appearance"),Appearance) || Appearance.Len()!=64
                || !VectorArray(Mob,TEXT("position"),Position,30000000) || (Position-Out.Origin).GetAbs().GetMax()>100000
                || !Number(Mob,TEXT("yaw"),-1e9,1e9,Yaw) || !Number(Mob,TEXT("health"),.01,10000,Health)) return false;
            for(TCHAR C2:Appearance) if(!((C2>='0'&&C2<='9') || (C2>='a'&&C2<='f'))) return false;
            const FIntVector MobCell(FMath::FloorToInt(Position.X/8),FMath::FloorToInt(Position.Y/8),FMath::FloorToInt(Position.Z/8));
            if(FMath::Abs(MobCell.X-Out.Center.X)>Out.Radius || FMath::Abs(MobCell.Y-Out.Center.Y)>Out.HalfHeight || FMath::Abs(MobCell.Z-Out.Center.Z)>Out.Radius) return false;
            Seen.Add(MobId);
        }
        Out.Mobs=*Mobs;
    }
    return true;
}
bool FBridgeNativeWorldStore::BeginLoad(const FString& Requested,ABridgeWorld* World,const FVector& Anchor,UMaterialInterface* InMaterial,UBridgeBlockPalette* InPalette) {
    Cancel();Error.Empty();Metadata=FBridgeNativeWorldMetadata();Target=World;Material=InMaterial;Palette=InPalette;FeetAnchor=Anchor;
    if(!IsValid(World) || !IsValid(InPalette)) {Fail(TEXT("Offline play requires an imported block palette and a terrain world"));return false;}
    FString Path=FPaths::ConvertRelativePathToFull(Requested);FPaths::NormalizeFilename(Path);
    if(Path.EndsWith(TEXT(".json"))) {
        auto& Platform=FPlatformFileManager::Get().GetPlatformFile();const int64 Size=Platform.FileSize(*Path);FString Text,Schema;
        if(Size<=0 || Size>1024*1024 || !FFileHelper::LoadFileToString(Text,*Path) || !ParseJson(Text,Metadata.Manifest)
            || !Metadata.Manifest->TryGetStringField(TEXT("schema"),Schema) || Schema!=TEXT("uebridge.native.v1")) {Fail(TEXT("Invalid native_manifest.json"));return false;}
        const TSharedPtr<FJsonObject>* WorldFile=nullptr;FString Relative;
        if(!Metadata.Manifest->TryGetObjectField(TEXT("world"),WorldFile) || !(*WorldFile)->TryGetStringField(TEXT("file"),Relative)) {Fail(TEXT("Native manifest has no world.file"));return false;}
        FPaths::NormalizeFilename(Relative);TArray<FString> Parts;Relative.ParseIntoArray(Parts,TEXT("/"),false);
        if(Relative.IsEmpty() || Relative.Len()>256 || !FPaths::IsRelative(Relative) || Relative.Contains(TEXT(":")) || Parts.Contains(TEXT(".."))) {Fail(TEXT("Native world.file must stay inside its export folder"));return false;}
        Path=FPaths::Combine(FPaths::GetPath(Path),Relative);FPaths::CollapseRelativeDirectories(Path);
    }
    ActiveFile=Path;if(!OpenReader(Path)) return false;FString Text;bool End;
    FBridgeNativeWorldMetadata HeaderData;
    if(!ReadLine(Text,End) || End || !ReadHeader(Text,HeaderData)) {Fail(TEXT("Invalid offline world header"));return false;}
    HeaderData.Manifest=Metadata.Manifest;HeaderData.SourceFile=Path;
    HeaderData.SaveFile=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("NativeWorlds"),HeaderData.PackageId+TEXT(".ndjson"));Metadata=MoveTemp(HeaderData);
    if(FPlatformFileManager::Get().GetPlatformFile().FileExists(*Metadata.SaveFile)) {
        ActiveFile=Metadata.SaveFile;if(!OpenReader(ActiveFile)) return false;
        FBridgeNativeWorldMetadata Saved;
        if(!ReadLine(Text,End) || End || !ReadHeader(Text,Saved) || Saved.PackageId!=Metadata.PackageId || Saved.Dimension!=Metadata.Dimension) {Fail(TEXT("Saved offline world header is invalid; save and source are preserved"));return false;}
        Saved.Manifest=Metadata.Manifest;Saved.SourceFile=Metadata.SourceFile;Saved.SaveFile=Metadata.SaveFile;Metadata=MoveTemp(Saved);
    }
    ProcessedCells=0;ProcessedRows=0;SeenCells.Empty();ValidatedModels.Empty();State=EState::Validating;
    ReadTimestamp=FPlatformFileManager::Get().GetPlatformFile().GetTimeStamp(*ActiveFile);
    UE_LOG(LogTemp,Display,TEXT("Bridge native world: validating %s (%d cells)"),*ActiveFile,Metadata.Cells);return true;
}
bool FBridgeNativeWorldStore::ParseCell(const FString& Text,FBridgePacket& Out,bool CheckModels) {
    TSharedPtr<FJsonObject> O;FString Type;FVector C;
    if(!ParseJson(Text,O) || !O->TryGetStringField(TEXT("type"),Type) || Type!=TEXT("cell") || !VectorArray(O,TEXT("cell"),C,3750000)
        || C.X!=FMath::FloorToDouble(C.X) || C.Y!=FMath::FloorToDouble(C.Y) || C.Z!=FMath::FloorToDouble(C.Z)) return false;
    Out=FBridgePacket();Out.Kind=EBridgeKind::WorldCell;Out.Cell=FIntVector(int32(C.X),int32(C.Y),int32(C.Z));
    if(FMath::Abs(Out.Cell.X-Metadata.Center.X)>Metadata.Radius || FMath::Abs(Out.Cell.Y-Metadata.Center.Y)>Metadata.HalfHeight || FMath::Abs(Out.Cell.Z-Metadata.Center.Z)>Metadata.Radius || SeenCells.Contains(Out.Cell)) return false;
    const TArray<TSharedPtr<FJsonValue>> *Types=nullptr,*Rows=nullptr;
    if(!O->TryGetArrayField(TEXT("palette"),Types) || Types->Num()>512 || !O->TryGetArrayField(TEXT("blocks"),Rows) || Rows->Num()>512) return false;
    TArray<FBridgeBlock> Descriptors;Descriptors.Reserve(Types->Num());
    for(const auto& Value:*Types) {
        const TArray<TSharedPtr<FJsonValue>>* Fields=nullptr;FBridgeBlock B;int32 Color,Opacity,Emission;
        if(!Value.IsValid() || !Value->TryGetArray(Fields) || Fields->Num()!=5 || !(*Fields)[0]->TryGetString(B.BlockId) || !Identifier(B.BlockId)
            || !(*Fields)[1]->TryGetString(B.StateKey) || !StateKey(B.StateKey) || !ArrayInt(*Fields,2,0,0xffffff,Color) || !ArrayInt(*Fields,3,0,15,Opacity) || !ArrayInt(*Fields,4,0,15,Emission)) return false;
        B.Color=Color;B.Opacity=uint8(Opacity);B.Emission=uint8(Emission);B.Role=1;B.NativeCompact=B.HasSourceBlock=B.HasLight=true;
        const FString Key=B.BlockId+TEXT("[")+B.StateKey+TEXT("]");
        if(CheckModels && !ValidatedModels.Contains(Key)) {
            if(ValidatedModels.Num()>=65536) return false;TArray<FBridgeModelFace> Faces;TArray<FBox> Collision,Outline;
            if(!Palette.IsValid() || !Palette->GetStateBoxes(B.BlockId,B.StateKey,Collision,Outline) || !Palette->BuildModel(B.BlockId,B.StateKey,Faces)) {Error=TEXT("Missing offline block model: ")+Key;return false;}
            for(const auto& Face:Faces) if(!Palette->FindFaceMaterial(Face.TextureId,Face.bTint)) {Error=TEXT("Missing offline face material: ")+Face.TextureId;return false;}
            ValidatedModels.Add(Key);
        }
        Descriptors.Add(MoveTemp(B));
    }
    TSet<int32> Owners;Out.Blocks.Reserve(Rows->Num());
    for(const auto& Value:*Rows) {
        const TArray<TSharedPtr<FJsonValue>>* Fields=nullptr;int32 Local,Descriptor,Sky,Light;
        if(!Value.IsValid() || !Value->TryGetArray(Fields) || Fields->Num()!=4 || !ArrayInt(*Fields,0,0,511,Local) || Owners.Contains(Local)
            || !ArrayInt(*Fields,1,0,Descriptors.Num()-1,Descriptor) || !ArrayInt(*Fields,2,0,15,Sky) || !ArrayInt(*Fields,3,0,15,Light)) return false;
        Owners.Add(Local);FBridgeBlock B=Descriptors[Descriptor];B.SourceBlock=Out.Cell*8+FIntVector(Local&7,Local>>6,(Local>>3)&7);
        if(B.SourceBlock.GetMax()>30000000 || B.SourceBlock.GetMin()< -30000000) return false;
        B.Position=FVector(B.SourceBlock)+FVector(.5)-Metadata.Origin;if(B.Position.GetAbs().GetMax()>100000) return false;
        B.SkyLight=uint8(Sky);B.BlockLight=uint8(Light);Out.Blocks.Add(MoveTemp(B));
    }
    if(O->HasField(TEXT("skyTop"))) {
        const TArray<TSharedPtr<FJsonValue>>* Top=nullptr;if(!O->TryGetArrayField(TEXT("skyTop"),Top) || Top->Num()!=64) return false;
        for(int32 I=0;I<64;++I) {int32 N;if(!ArrayInt(*Top,I,0,15,N)) return false;Out.SkyTop.Add(uint8(N));}
    }
    if(!BridgeBiomeTint::Read(O,Out.BiomeTints)) return false;
    if(O->HasField(TEXT("water"))) {
        const TArray<TSharedPtr<FJsonValue>>* Fluid=nullptr;
        if(!O->TryGetArrayField(TEXT("water"),Fluid) || Fluid->Num()>512) return false;
        TSet<int32> SeenWater;
        for(int32 I=0;I<Fluid->Num();++I) {int32 Index;if(!ArrayInt(*Fluid,I,0,511,Index) || SeenWater.Contains(Index)) return false;SeenWater.Add(Index);Out.Water.Add(uint16(Index));}
    }
    Out.TotalBatches=1;Out.BatchIndex=0;Out.CompactTerrain=true;Out.MinecraftOrigin=Metadata.Origin;
    Out.Sequence=Out.SnapshotSequence=PacketSequence++;Out.SnapshotId=ImportId;
    SeenCells.Add(Out.Cell);++ProcessedCells;ProcessedRows+=Out.Blocks.Num();
    return ProcessedCells<=Metadata.Cells && ProcessedRows<=MaxRows;
}
bool FBridgeNativeWorldStore::StartApplying() {
    auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
    if(Platform.GetTimeStamp(*ActiveFile)!=ReadTimestamp || Platform.FileSize(*ActiveFile)!=FileSize) {Fail(TEXT("Offline world changed during validation"));return false;}
    if(!Target.IsValid() || !Palette.IsValid() || !OpenReader(ActiveFile)) return false;
    FString Text;bool End;FBridgeNativeWorldMetadata HeaderData;
    if(!ReadLine(Text,End) || End || !ReadHeader(Text,HeaderData) || HeaderData.PackageId!=Metadata.PackageId) {Fail(TEXT("Offline world changed during validation"));return false;}
    Metadata.Spawn=HeaderData.Spawn;Metadata.Yaw=HeaderData.Yaw;Metadata.Pitch=HeaderData.Pitch;
    PacketSequence=3;ImportId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    FBridgePacket Begin;Begin.Kind=EBridgeKind::WorldBegin;Begin.Sequence=1;Begin.ImportId=ImportId;Begin.MinecraftOrigin=Metadata.Origin;
    // Validation succeeded before the existing session-local terrain is replaced.
    Target->Clear();if(!Target->BeginImport(Begin,FeetAnchor)) {Fail(TEXT("Cannot begin offline terrain import"));return false;}
    FBridgePacket Scope;Scope.Kind=EBridgeKind::WorldScope;Scope.Sequence=2;Scope.Cell=Metadata.Center;Scope.Radius=Metadata.Radius;Scope.HalfHeight=Metadata.HalfHeight;
    if(!Target->Handle(Scope,FeetAnchor,Material.Get(),Palette.Get())) {Fail(TEXT("Offline terrain scope exceeds its memory budget"));return false;}
    if(Metadata.Environment) FBridgeLightingService::SetEnvironment(Target->GetWorld(),Metadata.Environment,false);
    SeenCells.Empty();ProcessedCells=0;ProcessedRows=0;State=EState::Loading;return true;
}
void FBridgeNativeWorldStore::Tick(double BudgetMs) {
    if(!IsLoading() && !IsSaving()) return;
    if(!Target.IsValid()) {Fail(TEXT("Offline terrain world is no longer available"));return;}
    const double Deadline=FPlatformTime::Seconds()+FMath::Clamp(BudgetMs,.1,50.)/1000;
    for(int32 Work=0;Work<32 && FPlatformTime::Seconds()<Deadline;++Work) {
        if(IsSaving()) {if(!SaveNextCell()) return;if(!IsSaving()) return;continue;}
        FString Text;bool End;if(!ReadLine(Text,End)) return;
        if(End) {
            if(FPlatformFileManager::Get().GetPlatformFile().GetTimeStamp(*ActiveFile)!=ReadTimestamp) {Fail(TEXT("Offline world changed while loading"));return;}
            if(ProcessedCells!=Metadata.Cells) {Fail(TEXT("Offline terrain is incomplete; missing cells are never treated as empty air"));return;}
            if(State==EState::Validating) {if(!StartApplying()) return;continue;}
            FBridgePacket Commit;Commit.Kind=EBridgeKind::WorldCommit;Commit.ImportId=ImportId;Commit.ImportCells=Metadata.Cells;
            if(!Target->CommitImport(Commit)) {Fail(TEXT("Offline terrain commit failed"));return;}
            Reader.Reset();State=EState::Ready;SavedSerial=Target->GetMutationSerial();
            UE_LOG(LogTemp,Display,TEXT("Bridge native world ready: cells=%d rows=%lld boundary=finite saved=%s"),ProcessedCells,ProcessedRows,*Metadata.SaveFile);return;
        }
        FBridgePacket P;if(!ParseCell(Text,P,State==EState::Validating)) {Fail(Error.IsEmpty() ? FString::Printf(TEXT("Invalid offline cell at line %d"),Line) : Error);return;}
        if(State==EState::Loading && !Target->Handle(P,FeetAnchor,Material.Get(),Palette.Get())) {Fail(TEXT("Offline cell import failed: ")+Target->GetModelError());return;}
    }
}
bool FBridgeNativeWorldStore::WriteLine(const TSharedPtr<FJsonObject>& Object) {
    FString Text;if(!Writer || !FJsonSerializer::Serialize(Object.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))) return false;
    FTCHARToUTF8 Bytes(*Text);if(Bytes.Length()>MaxLineBytes || Writer->Tell()+Bytes.Length()+1>MaxFileBytes) return false;
    const uint8 Newline='\n';return Writer->Write(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length()) && Writer->Write(&Newline,1);
}
bool FBridgeNativeWorldStore::BeginSave(ABridgeWorld* World,const FVector& Feet,const FRotator& Rotation,const TSharedPtr<FJsonObject>& RuntimeState) {
    if(State!=EState::Ready || !IsValid(World) || !World->IsSealed() || Feet.ContainsNaN() || Rotation.ContainsNaN()) {Error=TEXT("Offline world is not ready to save");return false;}
    Target=World;FBridgeNativeWorldMetadata Next=Metadata;
    if(!World->GetNativeScope(Next.Center,Next.Radius,Next.HalfHeight,Next.Origin)) return false;
    World->GetNativeCellKeys(SaveCells);Next.Cells=SaveCells.Num();Next.Spawn=Feet;Next.Yaw=Rotation.Yaw;Next.Pitch=FMath::Clamp(-double(Rotation.Pitch),-90.,90.);Next.RuntimeState=RuntimeState;
    SaveSnapshot.Empty();
    for(const auto& Cell:SaveCells) {
        auto& Snapshot=SaveSnapshot.Add(Cell);World->GetNativeWaterCell(Cell,Snapshot.Water);
        if(!World->GetNativeCell(Cell,Snapshot.Rows,Snapshot.SkyTop)) {SaveSnapshot.Empty();Error=TEXT("Cannot snapshot offline terrain; previous save preserved");return false;}
        World->GetNativeBiomeTintCell(Cell,Snapshot.BiomeTints);
    }
    const TArray<TSharedPtr<FJsonValue>>* CurrentMobs=nullptr;
    if(RuntimeState && RuntimeState->TryGetArrayField(TEXT("mobs"),CurrentMobs)) Next.Mobs=*CurrentMobs;
    auto Candidate=Header(Next);FString Encoded;FJsonSerializer::Serialize(Candidate.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Encoded));
    FBridgeNativeWorldMetadata Checked;if(!ReadHeader(Encoded,Checked)) {Error=TEXT("Cannot save invalid player position, runtime state or an incomplete scope");return false;}
    auto& Platform=FPlatformFileManager::Get().GetPlatformFile();if(!Platform.CreateDirectoryTree(*FPaths::GetPath(Metadata.SaveFile))) {Error=TEXT("Cannot create Saved/NativeWorlds");return false;}
    TemporaryFile=Metadata.SaveFile+TEXT(".tmp-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Writer.Reset(Platform.OpenWrite(*TemporaryFile,false,false));if(!Writer) {TemporaryFile.Empty();Error=TEXT("Cannot create offline save temporary file");return false;}
    SaveSerial=World->GetMutationSerial();SaveCursor=0;ProcessedRows=0;SaveHeader=Candidate;Error.Empty();State=EState::Saving;
    if(!WriteLine(Candidate)) {Fail(TEXT("Cannot write offline save header"));return false;}return true;
}
bool FBridgeNativeWorldStore::SaveNextCell() {
    if(!Target.IsValid()) {Fail(TEXT("World unavailable during saving; previous save preserved"));return false;}
    if(SaveCursor==SaveCells.Num()) {
        if(!Writer->Flush(true)) {Fail(TEXT("Cannot flush offline save; previous save preserved"));return false;}Writer.Reset();
        // Never delete an existing save before replacing it: a failed commit
        // leaves the previous file intact on Windows as well as POSIX hosts.
        if(!BridgeNativeFile::Replace(Metadata.SaveFile,TemporaryFile)) {Fail(TEXT("Cannot atomically replace offline save; previous save preserved"));return false;}
        TemporaryFile.Empty();SaveSnapshot.Empty();SavedSerial=SaveSerial;State=EState::Ready;Metadata.RuntimeState=SaveHeader->HasTypedField<EJson::Object>(TEXT("runtimeState")) ? SaveHeader->GetObjectField(TEXT("runtimeState")) : nullptr;
        FVector Position;if(VectorArray(SaveHeader,TEXT("spawn"),Position,30000000)) Metadata.Spawn=Position;
        SaveHeader->TryGetNumberField(TEXT("yaw"),Metadata.Yaw);SaveHeader->TryGetNumberField(TEXT("pitch"),Metadata.Pitch);
        Metadata.Mobs=SaveHeader->GetArrayField(TEXT("mobs"));
        UE_LOG(LogTemp,Display,TEXT("Bridge native world saved: %s (%d cells)"),*Metadata.SaveFile,SaveCells.Num());return true;
    }
    const FIntVector Cell=SaveCells[SaveCursor++];const auto* Snapshot=SaveSnapshot.Find(Cell);
    if(!Snapshot || Snapshot->Rows.Num()>512) {Fail(TEXT("Offline cell has invalid logical data; previous save preserved"));return false;}
    const auto& Rows=Snapshot->Rows;const auto& Top=Snapshot->SkyTop;
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("type"),TEXT("cell"));O->SetArrayField(TEXT("cell"),Vec(FVector(Cell)));
    TArray<TSharedPtr<FJsonValue>> Types,Blocks;TMap<FString,int32> TypeIndexes;TSet<int32> Owners;
    for(const auto& B:Rows) {
        const FIntVector OwnerVoxel=B.HasSourceBlock ? B.SourceBlock : FIntVector(FMath::FloorToInt((B.Position+Metadata.Origin).X),FMath::FloorToInt((B.Position+Metadata.Origin).Y),FMath::FloorToInt((B.Position+Metadata.Origin).Z));
        const FIntVector Local=OwnerVoxel-Cell*8;
        if(Local.GetMin()<0 || Local.GetMax()>7 || !Identifier(B.BlockId) || !StateKey(B.StateKey)) {Fail(TEXT("Offline save encountered invalid source metadata"));return false;}
        const int32 Index=Local.X+(Local.Z<<3)+(Local.Y<<6);if(Owners.Contains(Index)) {Fail(TEXT("Offline save encountered duplicate source voxels"));return false;}Owners.Add(Index);
        const FString Key=FString::Printf(TEXT("%s|%s|%d|%d|%d"),*B.BlockId,*B.StateKey,B.Color,B.Opacity,B.Emission);
        int32 Descriptor;const int32* Found=TypeIndexes.Find(Key);
        if(Found) Descriptor=*Found;else {
            Descriptor=Types.Num();TypeIndexes.Add(Key,Descriptor);
            Types.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueString>(B.BlockId),MakeShared<FJsonValueString>(B.StateKey),MakeShared<FJsonValueNumber>(B.Color),MakeShared<FJsonValueNumber>(B.Opacity),MakeShared<FJsonValueNumber>(B.Emission)}));
        }
        Blocks.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Index),MakeShared<FJsonValueNumber>(Descriptor),MakeShared<FJsonValueNumber>(B.SkyLight),MakeShared<FJsonValueNumber>(B.BlockLight)}));
    }
    O->SetArrayField(TEXT("palette"),Types);O->SetArrayField(TEXT("blocks"),Blocks);
    if(!Snapshot->BiomeTints.IsEmpty()) {
        const auto Tints=BridgeBiomeTint::Write(Snapshot->BiomeTints);
        if(!Tints.IsValid()) {Fail(TEXT("Offline save encountered invalid biome tint data"));return false;}
        O->SetObjectField(TEXT("biomeTints"),Tints);
    }
    TArray<TSharedPtr<FJsonValue>> Water;for(uint16 Index:Snapshot->Water) Water.Add(MakeShared<FJsonValueNumber>(Index));O->SetArrayField(TEXT("water"),Water);
    if(Top.Num()==64) {TArray<TSharedPtr<FJsonValue>> Light;for(uint8 N:Top) Light.Add(MakeShared<FJsonValueNumber>(N));O->SetArrayField(TEXT("skyTop"),Light);}
    ProcessedRows+=Rows.Num();if(ProcessedRows>MaxRows || !WriteLine(O)) {Fail(TEXT("Offline save exceeds its data budget or could not be written"));return false;}return true;
}
bool FBridgeNativeWorldStore::FlushSave(int32 MaxMilliseconds) {
    const double Deadline=FPlatformTime::Seconds()+FMath::Clamp(MaxMilliseconds,1,30000)/1000.;
    while(IsSaving() && FPlatformTime::Seconds()<Deadline) Tick(20);
    if(IsSaving()) {Fail(TEXT("Offline save timed out; previous save preserved"));return false;}
    return Error.IsEmpty();
}
