#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeItemWorld.generated.h"

UCLASS()
class UEBRIDGE_API ABridgeItemWorld : public AActor {
    GENERATED_BODY()
public:
    ABridgeItemWorld();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Bridge|Items") TObjectPtr<class UBridgeBlockPalette> Palette;
    /** Application receipts are repeated until item_resolve; generic UDP ACKs never mutate quantity. */
    TFunction<void(const FString& Tx,int32 Revision,const FString& Action,int32 Count,const FString& Reason)> Result;
    TFunction<void(class ABridgeDroppedItem* Item)> Lighting;
    TFunction<bool(const FVector& Position)> Contains;
    TFunction<void(const FVector& Position)> PickupSound;
    FString Drop(const FString& Tx,const FString& Item,const FString& Model,int32 Count,int32 MaxCount,const FVector& Position,const FVector& Velocity);
    void SetPickupDelay(const FString& Tx,float Seconds);
    /** Native spawn rollback only: no pickup/merged population can be cancelled. */
    bool CancelNativeDrop(const FString& Tx);
    bool CollectNativeItems(const FBox& Bounds,const TFunction<int32(const FString&,int32)>& Accept);
    bool Resolve(const FString& Tx,int32 Revision,int32 Accepted);
    /** Pause physics/pickup without releasing escrow. Full control/session exit calls Clear. */
    void SetAuthority(bool Active,class ACharacter* Player);
    /** Offline transactions do not require legacy UDP replay tombstones. */
    void SetNativeLocal(bool Value) {NativeLocal=Value;}
    TSet<FString> GetTransactionIds() const;
    void Clear();
    int32 AliveCount() const;
    TArray<FVector> CollisionAnchors() const;
    FString GetReason() const {return LastReason;}
    /** One canonical stack per actor, including stacks merged during native play. */
    TArray<TSharedPtr<class FJsonValue>> ExportNativeDrops(const FVector& Anchor,const FVector& SourceOrigin) const;
    /** Invalid data or a spawn failure preserves the previous local population. */
    bool ImportNativeDrops(const TArray<TSharedPtr<class FJsonValue>>& Drops,const FVector& Anchor,const FVector& SourceOrigin,TMap<FString,FString>& OutTransactionItems);
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    struct FEntry {
        TWeakObjectPtr<class ABridgeDroppedItem> Actor;
        FString Item,Model,Action=TEXT("spawned"),Reason;
        int32 Count=0,MaxCount=64,Revision=0,Requested=0;
        bool Pending=true,Spawned=false,Rejected=false;
        double LastSent=-1,NextPickup=0;
    };
    UPROPERTY() TArray<TObjectPtr<class ABridgeDroppedItem>> Actors;
    TMap<FString,FEntry> Entries;
    TWeakObjectPtr<class ACharacter> Player;
    bool Authority=false,NativeLocal=false;
    double MergeTime=0;
    FString LastReason=TEXT("not_dropped");
    void Refresh(class ABridgeDroppedItem* Actor);
    void Merge();
};
