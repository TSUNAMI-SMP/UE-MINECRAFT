#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeRealisticExplosion.generated.h"
UCLASS()
class UEBRIDGE_API ABridgeRealisticExplosion : public AActor {
    GENERATED_BODY()
public:
    ABridgeRealisticExplosion();
    static ABridgeRealisticExplosion* Spawn(UWorld* World,const FVector& Position,bool Realistic,int32 Quality);
    virtual void Tick(float DeltaSeconds) override;
    void ShowAge(float Seconds);
private:
    float Age=0;
    int32 Count=24;
    UPROPERTY() TObjectPtr<class UNiagaraComponent> Niagara;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Fire;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Smoke;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FireMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> SmokeMaterial;
};
