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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Player") TObjectPtr<class UMaterialInterface> OutlineMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Textures") TObjectPtr<class UBridgeBlockPalette> TexturePalette;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Player") TObjectPtr<class UBridgePlayerAppearance> PlayerAppearance;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Particles") TObjectPtr<class UMaterialInterface> VanillaParticleMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Particles") TObjectPtr<class UMaterialInterface> VanillaDeathPoofMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Mobs") TObjectPtr<class UBridgeMobPalette> MobPalette;
    /** Immutable local export; runtime saves stay under Saved/NativeWorlds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Native") FString NativeWorldFile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Native") bool PreferNativePlay = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Native") TObjectPtr<class UBridgeNativeUiPalette> NativeUiPalette;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Native") TObjectPtr<class UBridgeNativeSoundPalette> NativeSoundPalette;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Native") bool NativePlayActive = false;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Native") bool NativeCreative = true;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Native") bool NativeLighting = false;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Native") FString NativeStatus = TEXT("not started");
    bool IsNativeReady() const;
    bool IsNativeSaving() const;
    float GetNativeHealth() const;
    int32 GetNativeAirTicks() const {return FMath::Clamp(300-FMath::FloorToInt(NativeSubmergedSeconds*20),0,300);}
    bool IsNativeEyeInWater() const {return NativeEyeInWater;}
    const FString& GetNativeLastAction() const { return LastAction; }
    void SetNativeInput(float Forward,float Right,bool Jump,bool Sneak,bool Sprint,bool Flying,int32 Perspective,float UEYaw,float UEPitch);
    void NativeSelect(const FString& ItemId);
    void NativeAction(const FString& Action);
    /** Caller removes its exact slot/cursor only after spawning succeeds. */
    bool NativeDrop(const FString& ItemId,int32 Count);
    void NativeSetLighting(bool Enabled);
    bool NativeSave();
    void NativeRespawn();
    FString NativeCommand(const FString& Command);
    bool IsNativeReplay() const;
    bool NativeRealisticVisuals() const;
    int32 NativeRealisticQuality() const;
    bool NativeRecording() const;
    double GetNativeTimeOfDay() const {return NativeTimeOfDay;}
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
    friend class UBridgeCinematicCapture;
    class FSocket* Socket = nullptr;
    FString Session;
    FString InstanceId;
    uint64 LastSequence = 0;
    TMap<FString, double> SeenEvents;
    FVector Anchor = FVector::ZeroVector;
    FVector SourceOrigin = FVector::ZeroVector;
    bool Anchored = false;
    bool TerrainMovementReady=true;
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
    UPROPERTY() TObjectPtr<class ABridgeMobWorld> MobWorld;
    UPROPERTY() TObjectPtr<class ABridgeItemWorld> ItemWorld;
    struct FEventResult {bool Accepted=true;FString Reason;};
    TMap<FString,FEventResult> EventResults;
    struct FPendingFeedback { TSharedPtr<FJsonObject> Json; double Created=0,Sent=-1; };
    TMap<FString,FPendingFeedback> PendingFeedback;
    UPROPERTY() TArray<TObjectPtr<class ABridgeArrow>> Arrows;
    FBridgePacket LatestInput;
    uint64 LastActionSequence=0;
    double LastActionAt=-1;
    double LastPerformance=-1,LastLightActors=-1;
    double PerformanceSeconds=0;int32 PerformanceFrames=0;
    FString LastAction=TEXT("ready");
    TSharedPtr<class FBridgeNativeWorldStore> NativeStore;
    UPROPERTY() TObjectPtr<class ABridgeRealisticWorld> RealisticWorld;
    UPROPERTY() TObjectPtr<class UBridgeCinematicCapture> CinematicCapture;
    void NativeRealisticBlast(const FVector& Position,float Radius);
    bool NativeInitialized=false;
    bool NativeControllerConfigured=false,NativeRestoreFailed=false,NativeExitPrepared=false;
    FDelegateHandle NativeTearDownHandle;
    double NativeLastDiagnostic=-1,NativeLastAutosave=-1;
    TMap<FString,FString> NativeDropItems;
    TMap<FString,int32> NativeDropRevisions;
    struct FNativeFuse {FVector Position;double Deadline;FVector Velocity=FVector::ZeroVector;TWeakObjectPtr<class ABridgeBlockPreview> Visual;};
    float NativeFuseClock=0;
    bool PrimeNativeTnt(const FVector& Position,float Remaining=4,const FVector& Velocity=FVector::ZeroVector,bool Restoring=false);
    void TickNativeTnt(float DeltaSeconds);
    TArray<FNativeFuse> NativeFuses;
    TSet<FString> MissingNativeSounds;
    float NativeMasterVolume=1;
    FVector NativeRespawnPosition=FVector::ZeroVector;
    double NativeBowStart=-1;
    double NativeLastAttack=-100;
    double NativeLastFluidDamage=-100,NativeSubmergedSeconds=0;
    bool NativeEyeInWater=false;
    double NativeTimeOfDay=6000,NativeWorldTime=0,NativeTimeAccumulator=0;
    bool NativeDaylightCycle=true;
    void TickNativeTime(float DeltaSeconds);
    float NativeAttackDamage() const;
public:
    float GetNativeAttackCharge() const;
    float GetNativeAttackCooldownTicks() const;
    bool IsNativeAttackTargetAlive() const;
private:
    TMap<FString,float> NativeSoundVolumes;
    UPROPERTY() TObjectPtr<class USoundAttenuation> NativeSoundAttenuation;
    void BeginNativePlay();
    void PrepareNativeExit(class UWorld* World);
    void TickNativePlay(float DeltaSeconds);
    void LogDiagnostics(double Now,bool bForceLog=false);
    void PlayNativeSound(const FString& Id,const FVector& Position,float Volume=1,float Pitch=1,const FString& Category=TEXT(""));
    bool SpawnNativeDrop(const FString& ItemId,int32 Count,const FVector& Position,const FVector& Velocity,float PickupDelay=.5f,FString* Transaction=nullptr);
    bool DropNativeContainer(const FIntVector& Block);
    bool IgniteNativeTnt(const FIntVector& Block);
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
    void QueueMobSound(const FString& Sound,const FVector& Position);
    void SendStatus(const TSharedRef<class FInternetAddr>& Sender);
    void SendAck(const FBridgePacket& Packet,const TSharedRef<class FInternetAddr>& Sender,bool Accepted,const FString& Reason);
    bool EnsureMobWorld();
    bool EnsureItemWorld();
    void QueueItemResult(const FString& Tx,int32 Revision,const FString& Action,int32 Count,const FString& Reason);
    void SendPerformance();
    void LightActor(AActor* Actor);
    void Explode(const FVector& Position);
};
