#include "BridgeCharacterMovement.h"
#include "BridgeMovementMath.h"
#include "BridgeCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
void UBridgeCharacterMovement::PhysFlying(float DeltaTime,int32 Iterations) {
    if(DeltaTime<MIN_TICK_TIME || !UpdatedComponent) return;
    float Remaining=DeltaTime;
    while(Remaining>=MIN_TICK_TIME && Iterations<MaxSimulationIterations) {
        ++Iterations;
        const float Dt=GetSimulationTimeStep(Remaining,Iterations);Remaining-=Dt;
        FVector Input=FlightIntent;const float Length=Input.Size2D();
        if(Length>1) {Input.X/=Length;Input.Y/=Length;}
        const double Boost=FlightSprint ? 2 : 1;
        const auto X=BridgeMovementMath::flight(Velocity.X,Input.X*100*Boost,.91,Dt);
        const auto Y=BridgeMovementMath::flight(Velocity.Y,Input.Y*100*Boost,.91,Dt);
        const auto Z=BridgeMovementMath::flight(Velocity.Z,Input.Z*300,.6,Dt);
        Velocity=FVector(X.velocity,Y.velocity,Z.velocity);
        const FVector Delta(X.distance,Y.distance,Z.distance);FHitResult Hit;
        SafeMoveUpdatedComponent(Delta,UpdatedComponent->GetComponentQuat(),true,Hit);
        if(Hit.IsValidBlockingHit()) {
            HandleImpact(Hit,Dt,Delta);SlideAlongSurface(Delta,1-Hit.Time,Hit.Normal,Hit,true);
            Velocity=FVector::VectorPlaneProject(Velocity,Hit.Normal);
        }
    }
}


bool UBridgeCharacterMovement::HasFootSupport(const FVector& Feet,double DepthCm) const {
    if(!GetWorld() || !CharacterOwner || DepthCm<=0) return false;
    const auto* Capsule=CharacterOwner->GetCapsuleComponent();
    const double Width=Capsule->GetScaledCapsuleRadius()-1e-5;
    // Minecraft tests an axis-aligned box, not the rounded capsule bottom.
    // A thin epsilon below the feet counts contact with the support surface.
    const FVector Center=Feet-FVector(0,0,DepthCm*.5);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgeSneakSupport),false,CharacterOwner);
    const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
    return GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeBox(FVector(Width,Width,DepthCm*.5+1e-5)),Params,Response);
}

void UBridgeCharacterMovement::MoveAlongFloor(const FVector& InVelocity,float DeltaSeconds,FStepDownResult* OutStepDownResult) {
    auto* Bridge=Cast<ABridgeCharacter>(CharacterOwner);
    if(!Bridge || !Bridge->UEAuthority || !CharacterOwner->bIsCrouched || !IsMovingOnGround() || DeltaSeconds<MIN_TICK_TIME) {
        Super::MoveAlongFloor(InVelocity,DeltaSeconds,OutStepDownResult);return;
    }
    const FVector Feet=Bridge->GetMinecraftFeetPosition();
    const FVector Before=UpdatedComponent->GetComponentLocation();
    const FFindFloorResult BeforeFloor=CurrentFloor;
    FVector Delta=InVelocity*DeltaSeconds;
    // World UE(+X,+Y) maps to Minecraft(+Z,-X). Preserve the original
    // axis order because corner clipping depends on which axis is checked first.
    const auto Clipped=BridgeMovementMath::sneakMove(-Delta.Y,Delta.X,[&](double X,double Z) {
        return !HasFootSupport(Feet+FVector(Z,-X,0),MaxStepHeight);
    });
    Delta.X=Clipped.z;Delta.Y=-Clipped.x;
    Super::MoveAlongFloor(Delta/DeltaSeconds,DeltaSeconds,OutStepDownResult);
    const FVector After=UpdatedComponent->GetComponentLocation();
    // UE's rounded capsule rolls down the corner although Minecraft's square
    // footprint remains supported. Keep that footprint on the same top plane;
    // genuine steps down are allowed when that plane no longer has support.
    if(After.Z<Before.Z && HasFootSupport(FVector(After.X,After.Y,Feet.Z),1.0)) {
        FHitResult Lift;
        SafeMoveUpdatedComponent(FVector(0,0,Before.Z-After.Z),UpdatedComponent->GetComponentQuat(),true,Lift);
        if(!Lift.IsValidBlockingHit()) {
            if(!IsMovingOnGround()) SetMovementMode(MOVE_Walking);
            CurrentFloor=BeforeFloor;
            CurrentFloor.HitResult.Location=UpdatedComponent->GetComponentLocation();
            CurrentFloor.HitResult.ImpactPoint=FVector(After.X,After.Y,Feet.Z);
            if(OutStepDownResult) {OutStepDownResult->bComputedFloor=true;OutStepDownResult->FloorResult=CurrentFloor;}
            Velocity.Z=0;
        }
    }
}
