#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BridgeMobData.h"
#include "BridgeMobCharacter.generated.h"

UCLASS()
class UEBRIDGE_API ABridgeMobCharacter : public ACharacter {
    GENERATED_BODY()
public:
    ABridgeMobCharacter();
    bool Initialize(const FBridgeMobSnapshot& Snapshot,const FBridgeMobAppearance& Appearance,class ABridgeMobWorld* OwnerWorld);
    void SetAuthority(bool Active,ACharacter* Target);
    bool Hit(float Damage,const FVector& Direction);
    bool Alive() const { return Health>0; }
    FString ModelDiagnostic() const {return InitializationReason;}
    int32 ModelVertexCount() const {return VertexCount;}
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
    float Damage=0, Phase=0, WalkWeight=0, Decision=0, AttackCooldown=0, DeathAge=0, Stuck=0;
    FVector Wander=FVector::ZeroVector, LastPosition=FVector::ZeroVector;
    FRandomStream Random;
    FString InitializationReason=TEXT("not_initialized");
    int32 VertexCount=0;
    void Animate(float DeltaSeconds);
};
