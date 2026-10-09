#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeProtocol.h"
#include "BridgeVoxelIndex.h"
#include "BridgeWorkQueue.h"
#include "BridgeWorld.generated.h"

struct FBridgeWorldStage {
    uint64 Sequence=0;
    FString Id;
    int32 Total=0;
    double Deadline=0;
    TMap<int32,TArray<FBridgeBlock>> Batches;
    TArray<uint8> SkyTop;
    TArray<FIntVector> BiomeTints;
    TArray<uint16> Water;
    bool Compact=false;
};

/** Session-local terrain: imported cells, UE collision, aiming and authoritative edits. */
UCLASS()
class UEBRIDGE_API ABridgeWorld : public AActor {
    GENERATED_BODY()
public:
    ABridgeWorld();
    bool Handle(const FBridgePacket& P,const FVector& Anchor,class UMaterialInterface* Material,class UBridgeBlockPalette* Palette=nullptr);
    void Clear(uint64 Barrier=0);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void NewSource() { if(Sealed) { ClearBarrier=0; ScopeSequence=0; } }
    bool BeginImport(const FBridgePacket& P,const FVector& Anchor);
    bool CommitImport(const FBridgePacket& P);
    bool IsSealed() const { return Sealed; }
    bool IsImporting() const { return !ImportId.IsEmpty() && !Sealed; }
    FString GetImportId() const { return ImportId; }
    int32 ImportedCells() const { return Revisions.Num(); }
    UFUNCTION(BlueprintCallable,Category="Bridge|World") int32 RemoveBlocksInSphere(FVector Position,float RemovalRadius);
    bool Aim(const FVector& Start,const FRotator& Rotation,float Reach,FIntVector& Block,FVector& Normal,const AActor* Ignored=nullptr,FVector* HitPoint=nullptr) const;
    FVector BlockCenter(const FIntVector& Block) const;
    /** Resolve source metadata without relying on the frozen Minecraft player. */
    bool GetBlockInfo(const FIntVector& SourceVoxel,FString& BlockId,FColor& Tint) const;
    FIntVector SourceVoxelAt(const FVector& Position) const;
    double NativeTickMillis() const {return TickMillis;}
    double NativeRuleMillis() const {return RuleMillis;}
    double NativeFluidMillis() const {return FluidMillis;}
    double NativeRulePeakMillis() const {return RulePeakMillis;}
    void ResetNativePerformancePeaks() {RulePeakMillis=0;}
    int32 PendingNativeRules() const {return int32(RuleQueue.Num());}
    FString NativeRuleStatistics() const;
    TArray<FVector> NativeFallingCollisionAnchors() const;
    bool GetBlockState(const FIntVector& SourceVoxel,FString& BlockId,FString& StateKey) const;
    FString GetModelError() const { return LastModelError; }
    TFunction<void(const FString& Type,const FString& Block,const FVector& Position)> InteractionSound;
    bool GetBlockOutline(const FIntVector& SourceVoxel,TArray<FBox>& MinecraftBoxes) const;
    bool GetSupportingBlock(const FVector& Feet,FIntVector& SourceVoxel,FString& BlockId,FColor& Tint,FVector& ImpactPoint,const AActor* Ignored=nullptr) const;
    bool BreakBlock(const FIntVector& Block);
    FString PlaceBlock(const FIntVector& Block,const FString& BlockId,int32 Color,double Yaw=0,const FVector& Normal=FVector::UpVector,const FVector& HitPoint=FVector::ZeroVector);
    void EnableNativeRules(int32 RandomTicks=3);
    void SetNativeSkyDarkness(int32 Darkness) {NativeSkyDarkness=FMath::Clamp(Darkness,0,15);}
    bool SetNativeBlockState(const FIntVector& Block,const FString& Id,const FString& State,int32 Tint=0xffffff);
    TArray<TSharedPtr<class FJsonValue>> ExportNativeFalling() const;
    bool ImportNativeFalling(const TArray<TSharedPtr<class FJsonValue>>& Values);
    FString GetNativeFallingRestoreError() const {return NativeFallingRestoreError;}
    TFunction<bool(const FString&,const FVector&)> NativeRuleDrop;
    /** Preflight container drops before deleting its block; failed spawning retains the block and all contents. */
    TFunction<bool(const FIntVector&)> NativeBlockRemoving;
    TFunction<void(const FIntVector&)> NativePrimeTnt;
    TFunction<int32(const FIntVector&)> NativeContainerPower;
    TFunction<bool(const FIntVector&)> NativePlateOccupied;
    TFunction<bool(const FIntVector&,const FIntVector&)> NativeHopperTransfer;
    TFunction<bool(const FIntVector&,const FIntVector&)> NativeDropperEmit;
    bool UseBlock(const FIntVector& Block,bool TimedRelease=false);
    int32 CellCount() const { return Cells.Num(); }
    int32 ShapeCount() const { return Shapes; }
    void UpdateCollisionCenter(const FVector& UEFeet);
    void UpdateCollisionCenters(const FVector& UEFeet,const TArray<FVector>& ExtraFeet);
    class FBridgeLightingService* GetLighting() const {return Lighting.Get();}
    int32 RenderedFaceCount() const;
    int32 RenderSectionCount() const;
    int32 RebuildPending() const;
    bool ContainsUEPosition(const FVector& UEPosition) const;
    /** Conservative source query; older exports omit free fluid voxels. */
    void EnableNativeFluids(bool UltraWarm=false);
    int32 FluidAt(const FVector& Position,FVector* Flow=nullptr) const;
    bool SetFluid(const FIntVector& Position,int32 Kind,int32 Level=0);
    bool IsWaterAtUEPosition(const FVector& UEPosition) const;
    bool IsMovementReady(const FVector& UEFeet,const FVector& Velocity=FVector::ZeroVector) const;
    bool EnsureCollisionForPosition(const FVector& UEPosition);
    bool IsOpaqueVoxel(const FIntVector& Block) const;
    FString GetSurfaceReason() const {return SurfaceReason;}
    /** Snapshot logical source rows, including UE edits. Rendering/collision proxies are never persisted. */
    void GetNativeCellKeys(TArray<FIntVector>& Out) const;
    bool GetNativeCell(const FIntVector& Cell,TArray<FBridgeBlock>& Rows,TArray<uint8>& SkyTop) const;
    void GetNativeBiomeTintCell(const FIntVector& Cell,TArray<FIntVector>& Out) const;
    FColor RenderTintAt(const FIntVector& SourceVoxel,const FString& BlockId,int32 TintIndex=0) const;
    void GetNativeWaterCell(const FIntVector& Cell,TArray<uint16>& Water) const;
    bool GetNativeScope(FIntVector& OutCenter,int32& OutRadius,int32& OutHalfHeight,FVector& OutOrigin) const;
    uint64 GetMutationSerial() const { return MutationSerial; }
    TFunction<void(const FIntVector&)> NativeCellChanged;
    /** Read-only replay process only: replace recorded logical cells without drops/rules. */
    bool ApplyReplayCell(const FIntVector& Cell,const TArray<FBridgeBlock>& Rows,const TArray<uint16>& Water,const TArray<uint8>& Sky);
    /** Finish recorded cell edits before a paused offline frame is drawn. */
    bool FlushReplayUpdates();
private:
    UPROPERTY() TMap<FIntVector,TObjectPtr<class ABridgeBlockPreview>> Cells;
    TMap<FIntVector,int32> Counts;
    TMap<FIntVector,uint64> Revisions;
    TMap<FIntVector,FBridgeWorldStage> Stages;
    FIntVector Center=FIntVector::ZeroValue;
    int32 Radius=0, HalfHeight=0, Shapes=0;
    uint64 ScopeSequence=0, ClearBarrier=0;
    bool Scoped=false;
    bool Sealed=false;
    FString ImportId;
    FVector ImportOrigin=FVector::ZeroVector, ImportAnchor=FVector::ZeroVector;
    TMap<FIntVector,TArray<FBridgeBlock>> Stored;
    UPROPERTY() TObjectPtr<class UMaterialInterface> SavedMaterial;
    UPROPERTY() TObjectPtr<class UBridgeBlockPalette> SavedPalette;
    UPROPERTY() TArray<TObjectPtr<class UBoxComponent>> Boundary;
    TMap<FIntVector,double> ButtonRelease;
    TMap<FIntVector,TSet<FIntVector>> ButtonTimerOwners;
    void RefreshButtonTimersForCell(const FIntVector& Cell);
    void TickButtonTimers(double GameTime);
    friend class FBridgeButtonTimerTest;
    FString LastModelError;
    uint64 MutationSerial=0;
    double TickMillis=0,RuleMillis=0,FluidMillis=0,RulePeakMillis=0;
    mutable FString SurfaceReason=TEXT("not_sampled");
    TSharedPtr<class FBridgeLightingService> Lighting;
    TSharedPtr<class FBridgeLightingService> PendingLighting;
    TArray<FIntVector> LightSeedCells;
    int32 LightSeedCursor=0;
    bool PendingLightInitialized=false;
    FIntVector LightingCenter=FIntVector::ZeroValue,PendingLightingCenter=FIntVector::ZeroValue;
    int32 LightingRadius=0,LightingHeight=0,PendingLightingRadius=0,PendingLightingHeight=0;
    struct FOpaqueCell {uint64 Words[8]={};};
    TMap<FIntVector,FOpaqueCell> OpaqueCells;
    // Row offsets, not pointers: replacing a cell cannot leave dangling visuals.
    TMap<FIntVector,BridgeVoxelIndex::Cell> VisualRows;
    void IndexVisualCell(const FIntVector& Cell);
    TSet<FIntVector> RebuildQueue,LightQueue;
    bool InitialRelightQueued=false;
    FIntVector CollisionCenter=FIntVector::ZeroValue;
    bool HasCollisionCenter=false;
    TSet<FIntVector> PhysicsCells;
    TArray<FVector> AdditionalCollisionPositions;
    FVector PrimaryCollisionPosition=FVector::ZeroVector;
    TMap<FIntVector,TArray<FBridgeBlock>> EditedBlocks;
    TMap<FIntVector,TSet<FIntVector>> EditedCellOwners;
    TSet<FIntVector> RemovedBlocks;
    TMap<FIntVector,TArray<uint8>> SkyTops;
    TMap<FIntVector,TArray<FIntVector>> BiomeTintCells;
    TMap<FIntVector,TArray<uint16>> WaterCells;
    bool NativeRules=false,DeferringGrassVisuals=false;
    double RuleDelayedMillis=0,RulePropagationMillis=0,RuleGrassMillis=0,RuleFallingMillis=0;
    int32 NativeRandomTickSpeed=3,NativeSkyDarkness=0;
    int32 GrassSectionCursor=0;
    float NativeRuleClock=0;
    int64 NativeRuleTick=0;
    struct FRuleVoxelHash {std::size_t operator()(const FIntVector& V) const {return GetTypeHash(V);}};
    BridgeWorkQueue::UniqueQueue<FIntVector,FRuleVoxelHash> RuleQueue;
    TSet<FIntVector> GrassSections;
    TArray<FIntVector> SortedGrassSections;
    TMap<FIntVector,int64> RuleDelayed;
    TMap<FIntVector,int32> ComparatorPower;
    FRandomStream RuleRandom{173931};
    struct FNativeFall {FIntVector Source;FString Id,State;int32 Tint=0xffffff;FVector Position,Previous,Velocity=FVector::ZeroVector;TWeakObjectPtr<class ABridgeBlockPreview> Visual;int32 Age=0;int64 RetryDropTick=0;};
    TArray<FNativeFall> NativeFalls;
    FString NativeFallingRestoreError;
    void TickNativeRules(float DeltaSeconds);
    void QueueNativeRule(const FIntVector& Block);
    void EnqueueNativeRule(const FIntVector& Block);
    void UpdateNativeRule(const FIntVector& Block,bool Delayed=false);
    int32 NativeSignal(const FIntVector& Source,const FIntVector& Target,bool Wire=true) const;
    int32 NativePowerAt(const FIntVector& Block,bool Wire=true,const FIntVector* Ignore=nullptr) const;
    bool StartNativeFall(const FIntVector& Block,const FString& Id,const FString& State,int32 Tint);
    struct FNativeMutation {FIntVector Block;FString Id,State;int32 Tint=0xffffff;};
    bool ApplyNativeMutations(const TArray<FNativeMutation>& Changes);
    friend class FBridgeNativeRulesTest;
    bool NativeFluidsEnabled=false,FluidUltraWarm=false;
    TMap<FIntVector,double> FluidUpdates;
    double FluidClock=0;
    void TickFluids(float DeltaSeconds);
    void QueueFluid(const FIntVector& Position);
    void BuildBoundary();
    /** Shared by terrain and fluid translation units; floor division handles negative coordinates. */
    static FIntVector CellOf(const FIntVector& Block);
    bool Inside(const FIntVector& C) const;
    FIntVector OwnerOf(const FBridgeBlock& Block) const;
    void RebuildCell(const FIntVector& Cell);
    void QueueNeighbors(const FIntVector& Cell);
    void RefreshLogicalCell(const FIntVector& Cell);
    void MarkEdited(const FIntVector& Block,bool Reindex=true);
    bool NearCollision(const FIntVector& Cell) const;
    void ClearOpaqueVoxel(const FIntVector& Voxel);
    void SeedLightingCell(const FIntVector& Cell,class FBridgeLightingService* Service) const;
    void BeginLightingRecenter();
    bool SupportingLogical(const FVector& Feet,FIntVector& Voxel,FString& BlockId,FColor& Tint,FVector& Point) const;
    bool AppendState(const FIntVector& Block,const FString& BlockId,int32 Color,const FString& StateKey,TArray<FBridgeBlock>& Out) const;
    int32 PlacementTint(const FIntVector& Block,const FString& BlockId,int32 SuppliedColor) const;
    const FBridgeBlock* FindVisual(const FIntVector& Block) const;
    void UpdateConnections(const FIntVector& Block);
};
