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
    bool Inside(const FIntVector& C) const;
};
