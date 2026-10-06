#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeArrow.generated.h"

/** Optional local UE ballistic arrow, not an authoritative MC arrow replica. */
UCLASS()
class UEBRIDGE_API ABridgeArrow : public AActor {
    GENERATED_BODY()
public:
    ABridgeArrow();
    void Launch(const FVector& Direction, float Pull, class AActor* IgnoreActor);
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UProjectileMovementComponent> Movement;
private:
    UFUNCTION() void Stopped(const FHitResult& Hit);
};
