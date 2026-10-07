#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "BridgeBlockGeometry.h"
#include "BridgeBlockPalette.generated.h"

/** Local imported resources; IDs cross the bridge, texture images do not. */
UCLASS(BlueprintType)
class UEBRIDGE_API UBridgeBlockPalette : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Textures") TMap<FString,TObjectPtr<UMaterialInterface>> Materials;
    /** Version 2: texture identifier plus #0/#1 tint flag, with BlockColor/BridgeUnlit parameters. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Models") TMap<FString,TObjectPtr<UMaterialInterface>> FaceMaterials;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Models") TMap<FString,FString> BlockstateDefinitions;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Models") TMap<FString,FString> Models;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Models") TMap<FString,FString> StateShapes;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Items") TMap<FString,FString> ItemModels;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Items") TMap<FString,TObjectPtr<UMaterialInterface>> ItemMaterials;
    bool BuildItem(const FString& ItemId,const FString& Context,TArray<FBridgeModelFace>& Out) const;
    bool BuildModel(const FString& BlockId,const FString& StateKey,TArray<FBridgeModelFace>& Out) const;
    bool GetStateBoxes(const FString& BlockId,const FString& StateKey,TArray<FBox>& Collision,TArray<FBox>& Outline) const;
    FString DefaultState(const FString& BlockId) const;
    FVector GetModelOffset(const FString& BlockId,const FIntVector& SourceBlock) const;
    bool HasSolidFace(const FString& BlockId,const FString& StateKey,const FString& Face) const;
    bool CannotConnect(const FString& BlockId,const FString& StateKey) const;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override { ModelCache.Empty();ShapeCache.Empty();Super::PostEditChangeProperty(Event); }
#endif
    UMaterialInterface* FindFaceMaterial(const FString& TextureId,bool bTint) const {
        const auto* Material=FaceMaterials.Find(TextureId+(bTint ? TEXT("#1") : TEXT("#0")));
        return Material ? Material->Get() : nullptr;
    }
    /** Vanilla block dust uses the model's particle sprite rather than the cube's rendered faces. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Particles") TMap<FString,TObjectPtr<UTexture2D>> ParticleTextures;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Particles") TMap<FString,bool> ParticleTints;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Particles") TMap<FString,FColor> ParticleColors;
    UMaterialInterface* Find(const FString& BlockId) const {
        const auto* Material=Materials.Find(BlockId); return Material ? Material->Get() : nullptr;
    }
    UTexture2D* FindParticleTexture(const FString& BlockId) const {
        const auto* Texture=ParticleTextures.Find(BlockId); return Texture ? Texture->Get() : nullptr;
    }
    FColor ParticleTint(const FString& BlockId) const {
        const auto* Color=ParticleColors.Find(BlockId); return Color ? *Color : FColor::White;
    }
private:
    // Resource palettes are immutable while Play runs. Cache model baking per state, not per cell.
    mutable TMap<FString,TArray<FBridgeModelFace>> ModelCache;
    mutable TMap<FString,TSharedPtr<class FJsonObject>> ShapeCache;
    TSharedPtr<class FJsonObject> ReadShapes(const FString& BlockId) const;
};
