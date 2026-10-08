#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BridgeMobMovement.generated.h"
UCLASS()
class UEBRIDGE_API UBridgeMobMovement : public UCharacterMovementComponent {
    GENERATED_BODY()
public:
    double ImpulseUntil=-1;
    virtual float GetMaxSpeed() const override;
protected:
    virtual void PhysFalling(float DeltaTime,int32 Iterations) override;
};
