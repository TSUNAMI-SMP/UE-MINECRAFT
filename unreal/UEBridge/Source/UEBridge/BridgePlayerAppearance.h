#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BridgePlayerAppearance.generated.h"

/** User-owned Minecraft skin, imported locally without bundling Minecraft assets. */
UCLASS(BlueprintType)
class UEBRIDGE_API UBridgePlayerAppearance : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Player") TObjectPtr<class UMaterialInterface> SkinMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Player") bool IsSlim=false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Player") FString PlayerName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Player") FString SkinHash;
};
