#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeNiagaraWater.generated.h"

/** One bounded, continuously emitting instance of an installed liquid asset. */
UCLASS()
class UEBRIDGE_API ABridgeNiagaraWater : public AActor {
    GENERATED_BODY()
public:
    ABridgeNiagaraWater();
    static class UNiagaraSystem* LoadTemplate(FString& Error);
    bool Initialize(class UNiagaraSystem* System,const FVector& Nozzle,FString& Error);
    void SetRunning(bool Running);
    bool IsRunning() const {return Running;}
    FVector SourcePosition=FVector::ZeroVector;
private:
    UPROPERTY() TObjectPtr<class UNiagaraComponent> Liquid;
    bool Running=false;
};
