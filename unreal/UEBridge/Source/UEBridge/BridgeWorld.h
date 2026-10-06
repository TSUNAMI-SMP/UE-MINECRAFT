#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeProtocol.h"
#include "BridgeWorld.generated.h"

struct FBridgeWorldStage {
    uint64 Sequence=0;
    FString Id;
    int32 Total=0;
    double Deadline=0;
    TMap<int32,TArray<FBridgeBlock>> Batches;
};

/** Session-local streamed visual cells. Never writes a Minecraft world or UE asset. */
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
    UFUNCTION(BlueprintCallable,Category="Bridge|World") int32 RemoveBlocksInSphere(FVector Position,float Radius);
    int32 CellCount() const { return Cells.Num(); }
    int32 ShapeCount() const { return Shapes; }
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
    void BuildBoundary();
    bool Inside(const FIntVector& C) const;
};
