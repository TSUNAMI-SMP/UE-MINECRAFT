#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "BridgeBlockPalette.generated.h"

/** Local imported resources; IDs cross the bridge, texture images do not. */
UCLASS(BlueprintType)
class UEBRIDGE_API UBridgeBlockPalette : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Textures") TMap<FString,TObjectPtr<UMaterialInterface>> Materials;
    UMaterialInterface* Find(const FString& BlockId) const {
        const auto* Material=Materials.Find(BlockId); return Material ? Material->Get() : nullptr;
    }
};
