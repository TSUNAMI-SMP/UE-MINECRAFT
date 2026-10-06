#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BridgeCharacter.generated.h"

UCLASS()
class UEBRIDGE_API ABridgeCharacter : public ACharacter {
    GENERATED_BODY()
public:
    ABridgeCharacter();
    virtual void Tick(float DeltaSeconds) override;
    void SetAuthorityEnabled(bool Enabled);
    void ApplyUEInput(float Forward,float Right,bool JumpHeld,bool Sneak);
    UPROPERTY(BlueprintReadOnly,Category="Bridge") bool UEAuthority=false;
    bool PreviousJump=false;
    void ApplyMinecraftPose(double BodyHeight, double EyeHeight, bool Sneak);
    UPROPERTY(BlueprintReadOnly, Category="Bridge") bool BridgeSneaking = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCameraComponent> BridgeCamera;
};
