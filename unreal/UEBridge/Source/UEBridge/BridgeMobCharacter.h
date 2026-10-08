#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BridgeMobData.h"
#include "BridgeMobCharacter.generated.h"

UCLASS()
class UEBRIDGE_API ABridgeMobCharacter : public ACharacter {
    GENERATED_BODY()
public:
    ABridgeMobCharacter(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    bool Initialize(const FBridgeMobSnapshot& Snapshot,const FBridgeMobAppearance& Appearance,class ABridgeMobWorld* OwnerWorld);
    void SetAuthority(bool Active,ACharacter* Target);
    bool Hit(float Damage,const FVector& Direction);
    void ApplyNativeKnockback(float StrengthBlocksPerTick,const FVector& AwayDirection);
    bool Alive() const { return Health>0; }
    FString ModelDiagnostic() const {return InitializationReason;}
    int32 ModelVertexCount() const {return VertexCount;}
    FBridgeMobSnapshot NativeSnapshot(const FVector& Anchor,const FVector& SourceOrigin) const;
    float GetNativeViewPitch() const {return NativeViewPitch;}
    FVector GetNativeFeet() const;
    float GetNativeWidthCm() const {return InitialSnapshot.Width*100.f;}
    float GetNativeHeightCm() const {return InitialSnapshot.Height*100.f;}
    void SetNativeViewPitch(float Pitch) {NativeViewPitch=FMath::Clamp(Pitch,-90.f,90.f);}
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mob") FString MinecraftId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mob") FString MinecraftType;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mob") float Health=20;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mob") float MaxHealth=20;
private:
    UPROPERTY() TObjectPtr<class USceneComponent> VisualRoot;
    UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> Parts;
    TArray<FBridgeMobPart> Poses;
    TWeakObjectPtr<class ABridgeMobWorld> WorldOwner;
    TWeakObjectPtr<ACharacter> Target;
    bool Enabled=false, Hostile=false;
    float Damage=0, Phase=0, WalkWeight=0, DeathAge=0;
    FVector GoalPosition=FVector::ZeroVector, Waypoint=FVector::ZeroVector, LastPosition=FVector::ZeroVector;
    FVector MoveIntent=FVector::ZeroVector;
    FVector SwimVector=FVector::ZeroVector;
    double AiAccumulator=0;
    int32 AiTicks=0,AttackTicks=0,JumpTicks=0,PathTicks=0,PanicTicks=0,BlockedTicks=0;
    bool HasGoal=false,HasWaypoint=false,BatRoosting=false;
    float GoalSpeed=1,FishSpeed=0,HeadYaw=0;
    float LastWaypointDistance=-1;
    int32 StalledPathTicks=0;
    int32 LookTicks=0,TargetUnseenTicks=0;bool LookAtPlayer=false,WasChasing=false;float IdleLookYaw=0;
    float ThrustTimer=0,ThrustSpeed=.2f;
    FRandomStream Random;
    FString InitializationReason=TEXT("not_initialized");
    FBridgeMobSnapshot InitialSnapshot;
    int32 VertexCount=0;
    float NativeViewPitch=0;
    double LastFullHit=-100,DeathStarted=-1,KnockbackUntil=-1;
    int64 LastImpulseTick=-1;bool ImpulseGrounded=false;
    float PreviousDamage=0,HurtRemaining=0,GroundSpeed=0;
    FVector DeathRootPosition=FVector::ZeroVector;
    void Animate(float DeltaSeconds);
    void TickNativeAI();
    bool FindGroundWaypoint(const FVector& Destination,FVector& Next);
    bool ProbeGround(const FVector& Seed,float PreviousZ,FVector& Feet) const;
    bool ClearBody(const FVector& Feet) const;
    bool InWater(const FVector& Position) const;
};
