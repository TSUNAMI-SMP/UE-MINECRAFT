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
    int32 FluidKind=0;FVector FluidFlow=FVector::ZeroVector;bool FluidJump=false,FluidSneak=false;
    void SetNativeFluid(int32 Kind,const FVector& Flow);
protected:
    virtual void PhysCustom(float DeltaTime,int32 Iterations) override;
    virtual void PhysWalking(float DeltaTime,int32 Iterations) override;
    virtual void PhysFlying(float DeltaTime,int32 Iterations) override;
    virtual void MoveAlongFloor(const FVector& InVelocity,float DeltaSeconds,FStepDownResult* OutStepDownResult=nullptr) override;
private:
    bool HasFootSupport(const FVector& Feet,double DepthCm) const;
};
