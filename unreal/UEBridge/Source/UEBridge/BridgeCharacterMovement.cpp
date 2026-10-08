#include "BridgeCharacterMovement.h"
#include "BridgeMovementMath.h"
void UBridgeCharacterMovement::PhysFlying(float DeltaTime,int32 Iterations) {
    if(DeltaTime<MIN_TICK_TIME || !UpdatedComponent) return;
    float Remaining=DeltaTime;
    while(Remaining>=MIN_TICK_TIME && Iterations<MaxSimulationIterations) {
        ++Iterations;
        const float Dt=GetSimulationTimeStep(Remaining,Iterations);Remaining-=Dt;
        FVector Input=FlightIntent;const float Length=Input.Size2D();
        if(Length>1) {Input.X/=Length;Input.Y/=Length;}
        const double Boost=FlightSprint ? 2 : 1;
        const auto X=BridgeMovementMath::flight(Velocity.X,Input.X*98*Boost,.91,Dt);
        const auto Y=BridgeMovementMath::flight(Velocity.Y,Input.Y*98*Boost,.91,Dt);
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
