#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeProtocol.h"
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video") int32 VideoPort = 7780;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bridge|Video") TObjectPtr<class UBridgeVideo> Video;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") TObjectPtr<class ACharacter> TargetCharacter;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") TObjectPtr<class UNiagaraSystem> ExplosionSystem;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") float ExplosionRadius = 400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") float Strain = 500000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge") float Force = 200000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Preview") TObjectPtr<class UMaterialInterface> PreviewMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Textures") TObjectPtr<class UBridgeBlockPalette> TexturePalette;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Player") TObjectPtr<class UBridgePlayerAppearance> PlayerAppearance;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Particles") TObjectPtr<class UMaterialInterface> VanillaParticleMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Bow") bool SpawnBowProjectiles = true;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") bool Connected = false;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") int32 InvalidPackets = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") int32 PreviewBlocks = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") int32 LastExplosionWalls = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge") float ForwardInput = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge") float RightInput = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge") bool JumpHeld = false;
    UPROPERTY(BlueprintReadOnly, Category="Bridge") bool SneakHeld = false;
    UPROPERTY(BlueprintReadOnly,Category="Bridge") bool UEControl=false;
    UFUNCTION(BlueprintCallable,Category="Bridge|World") int32 RemoveImportedBlocks(FVector Position,float Radius);
    UFUNCTION(BlueprintImplementableEvent, Category="Bridge") void OnJumpPressed();
    UFUNCTION(BlueprintImplementableEvent, Category="Bridge") void OnTntExplosion(FVector Position);
    UFUNCTION(BlueprintImplementableEvent, Category="Bridge") void OnBowFired(FVector Position, FVector Direction, float Pull);
private:
    class FSocket* Socket = nullptr;
    FString Session;
    FString InstanceId;
    uint64 LastSequence = 0;
    TMap<FString, double> SeenEvents;
    FVector Anchor = FVector::ZeroVector;
    bool Anchored = false;
    double LastInput = 0;
    double LastPacket = 0;
    double LastStatus = -1, LastPose=-1;
    uint64 PoseSequence=0;
    TSharedPtr<class FInternetAddr> Peer;
    uint32 PeerAddress = 0;
    int32 PeerPort = 0;
    UPROPERTY() TObjectPtr<class ABridgeBlockPreview> Preview;
    UPROPERTY() TObjectPtr<class ABridgeWorld> SyncedWorld;
    UPROPERTY() TObjectPtr<class ABridgeVanillaEffects> VanillaEffects;
    struct FPendingFeedback { TSharedPtr<FJsonObject> Json; double Created=0,Sent=-1; };
    TMap<FString,FPendingFeedback> PendingFeedback;
    UPROPERTY() TArray<TObjectPtr<class ABridgeArrow>> Arrows;
    FBridgePacket LatestInput;
    uint64 LastActionSequence=0;
    double LastActionAt=-1;
    FString LastAction=TEXT("ready");
    void BlockAction(const FBridgePacket& Packet);
    bool HasNewInput = false;
    uint64 PreviewGeneration = 0;
    FString StagingId;
    int32 ExpectedBatches = 0;
    bool SnapshotCompleted = false;
    double StagingDeadline = 0;
    TMap<int32, TArray<FBridgeBlock>> StagedBatches;
    void AcquireTarget();
    void Process(const FBridgePacket& Packet, const TSharedRef<class FInternetAddr>& Sender);
    bool HandleSnapshot(const FBridgePacket& Packet);
    void ClearPreview(uint64 Generation);
    void SendJson(const TSharedRef<class FJsonObject>& Json, const TSharedRef<class FInternetAddr>& Sender);
    void SendPose();
    void QueueFeedback(const FString& Type,const FString& BlockId,const FVector& Position,float FallDistance=0);
    void PumpFeedback(double Now);
    void SendStatus(const TSharedRef<class FInternetAddr>& Sender);
    void Explode(const FVector& Position);
};
