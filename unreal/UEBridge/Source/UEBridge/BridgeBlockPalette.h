#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "BridgeBlockPalette.generated.h"

/** Local imported resources; IDs cross the bridge, texture images do not. */
UCLASS(BlueprintType)
class UEBRIDGE_API UBridgeBlockPalette : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Textures") TMap<FString,TObjectPtr<UMaterialInterface>> Materials;
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
};
