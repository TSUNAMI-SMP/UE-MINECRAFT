#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BridgeCharacter.generated.h"

UCLASS()
class UEBRIDGE_API ABridgeCharacter : public ACharacter {
    GENERATED_BODY()
public:
    ABridgeCharacter();
    void ApplyMinecraftPose(double BodyHeight, double EyeHeight, bool Sneak);
    UPROPERTY(BlueprintReadOnly, Category="Bridge") bool BridgeSneaking = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCameraComponent> BridgeCamera;
};
