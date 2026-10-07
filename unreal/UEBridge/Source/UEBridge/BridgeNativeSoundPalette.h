#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sound/SoundWave.h"
#include "BridgeNativeSoundPalette.generated.h"

/** Local user-exported vanilla audio. No Minecraft audio is shipped in source. */
USTRUCT(BlueprintType)
struct FBridgeNativeSoundVariant {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundWave> Wave;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Volume = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Pitch = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Weight = 1;
};

USTRUCT(BlueprintType)
struct FBridgeNativeSoundEvent {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Id;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FBridgeNativeSoundVariant> Variants;
};

UCLASS(BlueprintType)
class UEBRIDGE_API UBridgeNativeSoundPalette : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native") TArray<FBridgeNativeSoundEvent> Events;
    const FBridgeNativeSoundEvent* Find(const FString& Id) const {
        for (const FBridgeNativeSoundEvent& Entry : Events) if (Entry.Id == Id) return &Entry;
        return nullptr;
    }
};
