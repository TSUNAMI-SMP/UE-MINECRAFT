#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeReceiver.generated.h"

/** Socket polling and UE mutations both occur on the game thread. */
UCLASS()
class UEBRIDGE_API ABridgeReceiver : public AActor {
    GENERATED_BODY()
public:
    ABridgeReceiver();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") int32 Port = 7779;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") TObjectPtr<class ACharacter> TargetCharacter;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") TObjectPtr<class UNiagaraSystem> ExplosionSystem;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") float ExplosionRadius = 400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") float Strain = 500000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") float Force = 200000.f;
    UPROPERTY(BlueprintReadOnly, Category="Bridge") float ForwardInput = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge") float RightInput = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge") bool JumpHeld = false;
    UFUNCTION(BlueprintImplementableEvent, Category="Bridge") void OnJumpPressed();
    UFUNCTION(BlueprintImplementableEvent, Category="Bridge") void OnTntExplosion(FVector Position);
private:
    class FSocket* Socket = nullptr;
    FString Session;
    uint64 LastSequence = 0;
    TMap<FString, double> SeenEvents;
    FVector Anchor = FVector::ZeroVector;
    double LastInput = 0;
    double LastPacket = 0;
    uint32 PeerAddress = 0;
    int32 PeerPort = 0;
    void Process(const TSharedPtr<class FJsonObject>& Packet, const TSharedRef<class FInternetAddr>& Sender);
    void Explode(const FVector& Position);
};
