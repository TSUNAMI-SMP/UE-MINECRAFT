#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeMobData.h"
#include "BridgeMobWorld.generated.h"

/** UE alone updates imported mob copies; no state is written back to a Minecraft server. */
UCLASS()
class UEBRIDGE_API ABridgeMobWorld : public AActor {
    GENERATED_BODY()
public:
    ABridgeMobWorld();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Bridge|Mobs") TObjectPtr<UBridgeMobPalette> Palette;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mobs") int32 Imported=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mobs") int32 MissingAppearance=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mobs") int32 Rejected=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mobs") FString LastReason=TEXT("not_imported");
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge|Mobs") float PlayerHealth=20;
    /** Fired on the game thread. Root receiver turns the native sound ID into reliable MC feedback. */
    TFunction<void(const FString& Sound,const FVector& Location)> Sound;
    bool Import(const FBridgeMobSnapshot& Snapshot,const FVector& Anchor);
    void SetAuthority(bool Active,class ACharacter* Player);
    void Clear();
    bool Attack(const FVector& Eye,const FVector& Direction,float Reach=500.f,float Damage=4.f);
    bool ReceiveArrow(const FVector& From,const FVector& To,float Damage);
    void NotifyMobSound(const FString& Type,const FString& Suffix,const FVector& Position);
    void HitPlayer(float Damage,const FVector& Position);
    void RespawnPlayer();
    int32 AliveCount() const;
    FString BehaviorDescription() const { return TEXT("ground wander/chase/melee; no species-specific AI/flight/swim/breeding/loot"); }
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TArray<TObjectPtr<class ABridgeMobCharacter>> Mobs;
    TSet<FString> SeenIds;
    bool Authority=false;
    TWeakObjectPtr<ACharacter> Player;
    double LastPlayerDamage=-1;
};
