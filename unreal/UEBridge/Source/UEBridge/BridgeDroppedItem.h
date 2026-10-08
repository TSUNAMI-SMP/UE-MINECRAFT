#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeDroppedItem.generated.h"

/** Session-local native GROUND item geometry; swept UE collision owns motion.
 * It has no Minecraft entity counterpart. BridgeItemWorld owns quantity/escrow. */
UCLASS()
class UEBRIDGE_API ABridgeDroppedItem : public AActor {
    GENERATED_BODY()
public:
    ABridgeDroppedItem();
    bool Initialize(class UBridgeBlockPalette* Palette,const FString& Model,int32 Quantity,const FVector& InitialVelocity);
    void SetQuantity(int32 Quantity);
    void SetActive(bool Value) {Active=Value;}
    const FString& GetModelKey() const {return ModelKey;}
    int32 GetQuantity() const {return Count;}
    float GetAge() const {return Age;}
    bool CanPickup() const {return Age>=PickupDelay;}
    void SetPickupDelay(float Seconds) {PickupDelay=FMath::Clamp(Seconds,0.f,10.f);}
    float GetPickupDelay() const {return PickupDelay;}
    const FVector& GetNativeVelocity() const {return Velocity;}
    /** File importer validates values before restoring a local simulation. */
    void RestoreNativeMotion(const FVector& SavedVelocity,float SavedAge) {Velocity=SavedVelocity;Age=SavedAge;}
    void MergeAge(float OtherAge) {Age=FMath::Min(Age,OtherAge);}
    TFunction<bool(const FVector& Position)> Contains;
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<class USphereComponent> Collision;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<class UBridgeBlockPalette> Palette;
    FString ModelKey;
    int32 Count=0,Copies=0;
    FVector Velocity=FVector::ZeroVector;
    float Age=0,Phase=0,VisualOffset=0,PickupDelay=.5f;
    bool Active=false;
    bool BuildMesh();
};
