#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BridgeCharacterMovement.generated.h"
UCLASS()
class UEBRIDGE_API UBridgeCharacterMovement : public UCharacterMovementComponent {
    GENERATED_BODY()
public:
    FVector FlightIntent=FVector::ZeroVector;
    bool FlightSprint=false;
protected:
    virtual void PhysFlying(float DeltaTime,int32 Iterations) override;
};
