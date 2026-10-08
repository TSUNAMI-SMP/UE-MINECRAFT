#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeProtocol.h"
#include "BridgeBlockPreview.generated.h"

/** Bounded block cells; optional solid collision for UE-owned initial imports. */
UCLASS()
class UEBRIDGE_API ABridgeBlockPreview : public AActor {
    GENERATED_BODY()
public:
    ABridgeBlockPreview();
    void Replace(const TArray<FBridgeBlock>& Blocks, const FVector& Anchor, class UMaterialInterface* Material, class UBridgeBlockPalette* Palette=nullptr, bool Physics=false,
        const TFunction<bool(const FIntVector&)>& OpaqueAt={},class FBridgeLightingService* Lighting=nullptr,bool RebuildVisual=true,
        const TFunction<FColor(const FIntVector&,const FString&,int32)>& RenderTintAt={},
        const TFunction<bool(const FIntVector&,FString&,FString&)>& BlockStateAt={});
    void Clear();
    void SetNativeFlash(bool Flash);
    bool ResolveHit(const class UPrimitiveComponent* Component,int32 Instance,FBridgeBlock& Out) const;
    void Relight(class FBridgeLightingService* Lighting);
    int32 FaceCount() const { return RenderedFaces; }
    int32 SectionCount() const { return DrawSections; }
    int32 ProxyCount() const { return NativeProxies; }
    bool HasContent() const {return RenderedFaces>0 || !Groups.IsEmpty();}
private:
    struct FLightVertex {FIntVector Voxel;FVector Local,Normal;float TintBlue=1;};
    TMap<int32,TArray<FLightVertex>> LightVertices;
    int32 RenderedFaces=0,DrawSections=0,NativeProxies=0;
    TMap<const class UPrimitiveComponent*,TArray<FBridgeBlock>> InstanceBlocks;
    UPROPERTY() TObjectPtr<class UStaticMesh> Cube;
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> Groups;
    UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> ModelGroups;
};
