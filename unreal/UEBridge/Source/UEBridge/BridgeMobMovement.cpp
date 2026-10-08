#include "BridgeMobMovement.h"
#include "BridgeCombatMath.h"
#include "Engine/World.h"
float UBridgeMobMovement::GetMaxSpeed() const {
    // UE's falling speed clamp must not discard a vanilla sprint impulse.
    if(GetWorld() && GetWorld()->GetTimeSeconds()<ImpulseUntil)
        return FMath::Max(Super::GetMaxSpeed(),float(Velocity.Size2D()));
    return Super::GetMaxSpeed();
}
void UBridgeMobMovement::PhysFalling(float DeltaTime,int32 Iterations) {
    if(DeltaTime<MIN_TICK_TIME || !UpdatedComponent) return;
    float Remaining=DeltaTime;
    while(Remaining>=MIN_TICK_TIME && Iterations<MaxSimulationIterations) {
        ++Iterations;const float Dt=FMath::Min(.05f,GetSimulationTimeStep(Remaining,Iterations));Remaining-=Dt;
        const FVector BeforeVelocity=Velocity;
        const auto Next=BridgeCombatMath::airVelocity({Velocity.X,Velocity.Y,Velocity.Z},Dt);
        // Fractional frames interpolate tick displacement without applying UE
        // terminal-speed/air-braking rules to the imported entity velocity.
        const auto Distance=BridgeCombatMath::airDistance({BeforeVelocity.X,BeforeVelocity.Y,BeforeVelocity.Z},Dt);
        const FVector Delta(Distance.x,Distance.y,Distance.z);FHitResult Hit;
        SafeMoveUpdatedComponent(Delta,UpdatedComponent->GetComponentQuat(),true,Hit);
        Velocity=FVector(Next.x,Next.y,Next.z);
        if(Hit.IsValidBlockingHit()) {
            if(BeforeVelocity.Z<=0 && IsValidLandingSpot(UpdatedComponent->GetComponentLocation(),Hit)) {
                ProcessLanded(Hit,Remaining,Iterations);return;
            }
            HandleImpact(Hit,Dt,Delta);SlideAlongSurface(Delta,1-Hit.Time,Hit.Normal,Hit,true);
            Velocity=FVector::VectorPlaneProject(Velocity,Hit.Normal);
        }
    }
}
