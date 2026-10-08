#include "BridgeLightingService.h"
#include "BridgeBlockPalette.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
uint64 StateLightingCacheEpoch=0;
BridgeLightingMath::Voxel Point(const FIntVector& P) {return {P.X,P.Y,P.Z};}
FIntVector Point(const BridgeLightingMath::Voxel& P) {return {P.x,P.y,P.z};}
int32 Cell(int32 Value) {return FMath::FloorToInt(Value/8.0);}
}
bool FBridgeLightingService::Reset(const FIntVector& Minimum,const FIntVector& Maximum,bool HasSky) {
    if(!Field.valid()) ++StateLightingCacheEpoch;
    DirtyMinCell=FIntVector(Cell(Minimum.X-1),Cell(Minimum.Y-1),Cell(Minimum.Z-1));
    const FIntVector EndCell(Cell(Maximum.X+1),Cell(Maximum.Y+1),Cell(Maximum.Z+1));
    DirtyCellSize=EndCell-DirtyMinCell+FIntVector(1,1,1);
    return Field.reset(Point(Minimum),Point(Maximum),HasSky);
}
void FBridgeLightingService::Clear() {Field.clear();++StateLightingCacheEpoch;}
void FBridgeLightingService::SetVoxel(const FIntVector& Voxel,uint8 Opacity,uint8 Emission,uint8 NativeSky,uint8 NativeBlock,uint8 SolidFaces) {Field.seed(Point(Voxel),NativeSky,NativeBlock);Field.set(Point(Voxel),Opacity,Emission,SolidFaces);}
void FBridgeLightingService::ClearVoxel(const FIntVector& Voxel) {Field.set(Point(Voxel),0,0);}
void FBridgeLightingService::SetSkyBoundary(int32 X,int32 Z,uint8 Light) {Field.setBoundary(X,Z,Light);}
void FBridgeLightingService::Initialize() {Field.initialize();}
void FBridgeLightingService::BeginInitialize() {Field.beginInitialize();}
uint32 FBridgeLightingService::Tick(uint32 Budget) {return static_cast<uint32>(Field.step(Budget));}
int32 FBridgeLightingService::Pending() const {return static_cast<int32>(Field.pending());}
void FBridgeLightingService::DiscardChanged() {Field.discardChanged();}
TSet<FIntVector> FBridgeLightingService::ConsumeChangedCells() {
    TSet<FIntVector> Result;
    const auto Changed=Field.consumeChanged();
    if(Changed.empty() || !Field.valid()) return Result;
    // At most 34^3 touched cells. Mark a compact bitset, then hash each cell once,
    // instead of repeatedly inserting one cell for every changed air-light voxel.
    const int32 Count=DirtyCellSize.X*DirtyCellSize.Y*DirtyCellSize.Z;
    TBitArray<> Bits(false,Count);
    for(const auto& P:Changed) {
        const int32 LowX=Cell(P.x-1),HighX=Cell(P.x+1),LowY=Cell(P.y-1),HighY=Cell(P.y+1),LowZ=Cell(P.z-1),HighZ=Cell(P.z+1);
        for(int32 X=LowX;X<=HighX;++X) for(int32 Y=LowY;Y<=HighY;++Y) for(int32 Z=LowZ;Z<=HighZ;++Z) {
            const int32 Index=((Y-DirtyMinCell.Y)*DirtyCellSize.Z+(Z-DirtyMinCell.Z))*DirtyCellSize.X+(X-DirtyMinCell.X);
            if(Index>=0 && Index<Count) Bits[Index]=true;
        }
    }
    for(TConstSetBitIterator<> It(Bits);It;++It) {
        int32 Index=It.GetIndex();const int32 X=Index%DirtyCellSize.X;Index/=DirtyCellSize.X;const int32 Z=Index%DirtyCellSize.Z,Y=Index/DirtyCellSize.Z;
        Result.Add(DirtyMinCell+FIntVector(X,Y,Z));
    }
    return Result;
}
FLinearColor FBridgeLightingService::Sample(const FIntVector& Voxel) const {
    const auto L=Field.sample(Point(Voxel));return FLinearColor(L.sky/15.f,L.block/15.f,1,1);
}
FString FBridgeLightingService::Describe(const FIntVector& Voxel) const {
    if(!Field.valid()) return TEXT("field=unavailable");
    const auto P=Point(Voxel);const auto L=Field.sample(P);const auto Low=Field.minimum(),High=Field.maximum();
    return FString::Printf(TEXT("voxel=(%d,%d,%d) inside=%s sky=%u block=%u opacity=%u emission=%u initialized=%s propagationPending=%llu bounds=(%d,%d,%d)..(%d,%d,%d) voxels=%llu"),
        Voxel.X,Voxel.Y,Voxel.Z,Field.inside(P)?TEXT("true"):TEXT("false"),unsigned(L.sky),unsigned(L.block),unsigned(Field.opacityAt(P)),unsigned(Field.emissionAt(P)),
        Field.ready()?TEXT("true"):TEXT("false"),static_cast<unsigned long long>(Field.pending()),Low.x,Low.y,Low.z,High.x,High.y,High.z,static_cast<unsigned long long>(Field.voxelCount()));
}
FLinearColor FBridgeLightingService::Vertex(const FIntVector& Voxel,const FVector& Local,const FVector& Normal) const {
    const FVector Abs=Normal.GetAbs();const int Axis=Abs.Y>=Abs.X && Abs.Y>=Abs.Z?1:(Abs.Z>=Abs.X?2:0);
    const int OtherA=(Axis+1)%3,OtherB=(Axis+2)%3;
    const int Sign=Normal[Axis]>=0?1:-1;
    FIntVector Base=Voxel;
    // An internal slab face is sampled in its own voxel; full boundary faces outside.
    if((Sign>0 && Local[Axis]>=.999) || (Sign<0 && Local[Axis]<=.001)) Base[Axis]+=Sign;
    FIntVector SideA=Base,SideB=Base,Corner=Base;
    SideA[OtherA]+=Local[OtherA]>.5?1:-1;SideB[OtherB]+=Local[OtherB]>.5?1:-1;
    Corner[OtherA]=SideA[OtherA];Corner[OtherB]=SideB[OtherB];
    const bool A=Field.opaque(Point(SideA)),B=Field.opaque(Point(SideB)),C=Field.opaque(Point(Corner));
    const auto Light=BridgeLightingMath::CornerLight(Field.sample(Point(Base)),Field.sample(Point(SideA)),Field.sample(Point(SideB)),Field.sample(Point((A&&B)?SideA:Corner)));
    return FLinearColor(Light[0],Light[1],BridgeLightingMath::FaceShade(Normal.X,Normal.Y,Normal.Z)*BridgeLightingMath::AO(A,B,C),1);
}
bool FBridgeLightingService::StateProperties(const UBridgeBlockPalette* Palette,const FString& BlockId,const FString& State,uint8& Opacity,uint8& Emission) {
    if(!Palette) return false;
    const FString* Text=Palette->StateShapes.Find(BlockId);TSharedPtr<FJsonObject> Shapes;
    if(!Text || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),Shapes) || !Shapes.IsValid()) return false;
    const TSharedPtr<FJsonObject>* States=nullptr,*Data=nullptr;
    // StateShapes stores the entire block entry, including native 'states'.
    if(!Shapes->TryGetObjectField(TEXT("states"),States) || !(*States)->TryGetObjectField(State,Data)) return false;
    double O=15,E=0;
    if(!(*Data)->TryGetNumberField(TEXT("opacity"),O) || !(*Data)->TryGetNumberField(TEXT("emission"),E)) return false;
    Opacity=static_cast<uint8>(FMath::Clamp(O,0.,15.));Emission=static_cast<uint8>(FMath::Clamp(E,0.,15.));return true;
}
uint8 FBridgeLightingService::StateFaceMask(const UBridgeBlockPalette* Palette,const FString& BlockId,const FString& State) {
    if(!Palette) return 0;
    // Palettes are immutable during Play. One block definition contains all state face masks.
    static TWeakObjectPtr<const UBridgeBlockPalette> LastPalette;
    static TMap<FString,TMap<FString,uint8>> Cached;
    static uint64 CachedEpoch=0;
    if(LastPalette.Get()!=Palette || CachedEpoch!=StateLightingCacheEpoch) {LastPalette=Palette;Cached.Empty();CachedEpoch=StateLightingCacheEpoch;}
    if(!Cached.Contains(BlockId)) {
        TMap<FString,uint8> StatesOut;TSharedPtr<FJsonObject> Definition;
        const FString* Text=Palette->StateShapes.Find(BlockId);
        const TSharedPtr<FJsonObject>* States=nullptr;
        if(Text && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),Definition) && Definition.IsValid() && Definition->TryGetObjectField(TEXT("states"),States)) {
            const FString Names[6]={TEXT("west"),TEXT("east"),TEXT("down"),TEXT("up"),TEXT("north"),TEXT("south")};
            for(const auto& Pair:(*States)->Values) {
                uint8 Mask=0;const auto Data=Pair.Value->AsObject();const TArray<TSharedPtr<FJsonValue>>* Faces=nullptr;
                if(Data.IsValid() && Data->TryGetArrayField(TEXT("solidFaces"),Faces)) for(const auto& Face:*Faces)
                    for(int Side=0;Side<6;++Side) if(Face->AsString()==Names[Side]) Mask|=1<<Side;
                // Native support faces on a transparent full cube (glass/leaves)
                // do not occlude light. Partial hulls retain directional shape blocking.
                const TArray<TSharedPtr<FJsonValue>>* Collision=nullptr;
                if(Data.IsValid() && Data->TryGetArrayField(TEXT("collision"),Collision) && Collision->Num()==1) {
                    const TArray<TSharedPtr<FJsonValue>>* Box=nullptr;
                    if((*Collision)[0]->TryGetArray(Box) && Box->Num()==6) {
                        bool Unit=true;
                        for(int Axis=0;Axis<3;++Axis) Unit=Unit && FMath::IsNearlyEqual((*Box)[Axis]->AsNumber(),0.) && FMath::IsNearlyEqual((*Box)[Axis+3]->AsNumber(),1.);
                        if(Unit) Mask=0;
                    }
                }
                StatesOut.Add(FString(*Pair.Key),Mask);
            }
        }
        Cached.Add(BlockId,MoveTemp(StatesOut));
    }
    return Cached.FindChecked(BlockId).FindRef(State);
}
bool FBridgeLightingService::SetEnvironment(UWorld* World,const TSharedPtr<FJsonObject>& Values,bool Vanilla,FString* FailureReason) {
    if(FailureReason) FailureReason->Empty();
    auto Fail=[&](const FString& Reason) {if(FailureReason) *FailureReason=Reason;return false;};
    if(!World) return Fail(TEXT("world_unavailable"));
    auto* Collection=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/Bridge/Minecraft/MPC_BridgeLighting_v1.MPC_BridgeLighting_v1"));
    auto* Instance=Collection?World->GetParameterCollectionInstance(Collection):nullptr;
    if(!Collection) return Fail(TEXT("lighting_collection_missing: run native project setup"));
    if(!Instance) return Fail(TEXT("lighting_collection_instance_unavailable"));
    if(!Instance->SetScalarParameterValue(TEXT("BridgeVanillaMode"),Vanilla?1:0)) return Fail(TEXT("lighting_parameter_missing: BridgeVanillaMode"));
    if(!Values.IsValid()) return true;
    const struct {const TCHAR* Json;const TCHAR* Parameter;double Fallback;} Scalars[]={
        {TEXT("skyFactor"),TEXT("BridgeSkyFactor"),1},{TEXT("blockFactor"),TEXT("BridgeBlockFactor"),1.5},
        {TEXT("ambient"),TEXT("BridgeAmbient"),0},{TEXT("gamma"),TEXT("BridgeGamma"),.5},{TEXT("nightVision"),TEXT("BridgeNightVision"),0},
        {TEXT("darkness"),TEXT("BridgeDarkness"),0},{TEXT("darkenWorld"),TEXT("BridgeDarkenWorld"),0}};
    for(const auto& S:Scalars) {
        double Value=S.Fallback;Values->TryGetNumberField(S.Json,Value);
        if(!FMath::IsFinite(Value)) return Fail(FString(TEXT("lighting_environment_nonfinite: "))+S.Json);
        if(!Instance->SetScalarParameterValue(S.Parameter,Value)) return Fail(FString(TEXT("lighting_parameter_missing: "))+S.Parameter);
    }
    for(const auto& Pair:TArray<TPair<FString,FString>>{{TEXT("skyColor"),TEXT("BridgeSkyColor")},{TEXT("ambientColor"),TEXT("BridgeAmbientColor")}}) {
        double Color=0xffffff;Values->TryGetNumberField(Pair.Key,Color);const uint32 RGB=static_cast<uint32>(Color);
        if(!Instance->SetVectorParameterValue(*Pair.Value,FLinearColor(((RGB>>16)&255)/255.f,((RGB>>8)&255)/255.f,(RGB&255)/255.f,1)))
            return Fail(TEXT("lighting_parameter_missing: ")+Pair.Value);
    }
    const TArray<TSharedPtr<FJsonValue>>* AmbientRGB=nullptr;
    if(Values->TryGetArrayField(TEXT("ambientColorRGB"),AmbientRGB) && AmbientRGB && AmbientRGB->Num()==3) {
        double RGB[3]={1,1,1};bool Valid=true;
        for(int32 Channel=0;Channel<3;++Channel) Valid=(*AmbientRGB)[Channel].IsValid() && (*AmbientRGB)[Channel]->TryGetNumber(RGB[Channel]) && FMath::IsFinite(RGB[Channel]) && RGB[Channel]>=0 && RGB[Channel]<=4 && Valid;
        if(Valid) Instance->SetVectorParameterValue(TEXT("BridgeAmbientColor"),FLinearColor(RGB[0],RGB[1],RGB[2],1));
    }
    return true;
}
void FBridgeLightingService::ApplyActor(AActor* Actor,const FIntVector& Voxel) const {
    if(!Actor) return;
    const FLinearColor Light=Sample(Voxel);
    TArray<UMeshComponent*> Meshes;Actor->GetComponents<UMeshComponent>(Meshes);
    for(auto* Mesh:Meshes) for(int32 I=0;I<Mesh->GetNumMaterials();++I) {
        auto* Source=Mesh->GetMaterial(I);if(!Source) continue;
        auto* Dynamic=Cast<UMaterialInstanceDynamic>(Source);
        if(!Dynamic) {Dynamic=UMaterialInstanceDynamic::Create(Source,Mesh);if(Dynamic) Mesh->SetMaterial(I,Dynamic);}
        if(Dynamic) {Dynamic->SetVectorParameterValue(TEXT("BridgeLight"),Light);Dynamic->SetScalarParameterValue(TEXT("BridgeUseVertexLight"),0);}
    }
}
