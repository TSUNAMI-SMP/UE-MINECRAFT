#include "BridgeWorld.h"
#include "BridgeBlockPreview.h"
#include "Engine/World.h"

ABridgeWorld::ABridgeWorld() { PrimaryActorTick.bCanEverTick=true; }
bool ABridgeWorld::Inside(const FIntVector& C) const {
    return Scoped && FMath::Abs(C.X-Center.X)<=Radius && FMath::Abs(C.Y-Center.Y)<=HalfHeight && FMath::Abs(C.Z-Center.Z)<=Radius;
}
void ABridgeWorld::Clear(uint64 Barrier) {
    for (auto& Pair:Cells) if (IsValid(Pair.Value)) Pair.Value->Destroy();
    Cells.Empty(); Counts.Empty(); Revisions.Empty(); Stages.Empty(); Shapes=0; Scoped=false; ScopeSequence=0; ClearBarrier=Barrier;
}
void ABridgeWorld::EndPlay(const EEndPlayReason::Type Reason) { Clear(); Super::EndPlay(Reason); }
void ABridgeWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds); const double Now=FPlatformTime::Seconds();
    for (auto It=Stages.CreateIterator();It;++It) if (Now>It.Value().Deadline) It.RemoveCurrent();
}
bool ABridgeWorld::Handle(const FBridgePacket& P,const FVector& Anchor,UMaterialInterface* Material) {
    if (P.Kind==EBridgeKind::WorldClear) { if (P.Sequence>=FMath::Max(ScopeSequence,ClearBarrier)) Clear(P.Sequence); return true; }
    if (P.Kind==EBridgeKind::WorldScope) {
        if (P.Sequence<=FMath::Max(ScopeSequence,ClearBarrier)) return true;
        Center=P.Cell; Radius=P.Radius; HalfHeight=P.HalfHeight; ScopeSequence=P.Sequence; Scoped=true;
        for (auto It=Cells.CreateIterator();It;++It) if (!Inside(It.Key())) {
            if (IsValid(It.Value())) It.Value()->Destroy(); Shapes-=Counts.FindRef(It.Key()); Counts.Remove(It.Key()); It.RemoveCurrent();
        }
        for (auto It=Revisions.CreateIterator();It;++It) if (!Inside(It.Key())) It.RemoveCurrent();
        // A sender completes its current cell before publishing a new scope.
        Stages.Empty(); return true;
    }
    if (P.Kind!=EBridgeKind::WorldCell) return false;
    if (!Inside(P.Cell) || P.SnapshotSequence<ScopeSequence || P.SnapshotSequence<=ClearBarrier
        || P.SnapshotSequence<=Revisions.FindRef(P.Cell)) return true;
    FBridgeWorldStage* Stage=Stages.Find(P.Cell);
    if (!Stage || P.SnapshotSequence>Stage->Sequence) {
        if (!Stage && Stages.Num()>=4) return false;
        FBridgeWorldStage Next; Next.Sequence=P.SnapshotSequence; Next.Id=P.SnapshotId; Next.Total=P.TotalBatches;
        Next.Deadline=FPlatformTime::Seconds()+60; Stages.Add(P.Cell,MoveTemp(Next)); Stage=Stages.Find(P.Cell);
    }
    if (P.SnapshotSequence<Stage->Sequence) return true;
    if (Stage->Id!=P.SnapshotId || Stage->Total!=P.TotalBatches) return false;
    if (!Stage->Batches.Contains(P.BatchIndex)) Stage->Batches.Add(P.BatchIndex,P.Blocks);
    if (Stage->Batches.Num()!=Stage->Total) return true;
    TArray<FBridgeBlock> Blocks; TSet<int32> Colors;
    for (int32 I=0;I<Stage->Total;++I) { const auto* Batch=Stage->Batches.Find(I); if (!Batch) return false; Blocks.Append(*Batch); }
    for (const auto& Block:Blocks) Colors.Add(Block.Color);
    const int32 NewCount=Shapes-Counts.FindRef(P.Cell)+Blocks.Num();
    if (Blocks.Num()>8192 || Colors.Num()>64 || NewCount>131072) return false;
    if (Blocks.IsEmpty()) {
        if (auto* Existing=Cells.Find(P.Cell)) { if (IsValid(*Existing)) (*Existing)->Destroy(); Cells.Remove(P.Cell); }
        Counts.Remove(P.Cell);
    } else {
        auto& Cell=Cells.FindOrAdd(P.Cell);
        if (!IsValid(Cell)) Cell=GetWorld()->SpawnActor<ABridgeBlockPreview>();
        if (!Cell) return false;
        Cell->Replace(Blocks,Anchor,Material); Counts.Add(P.Cell,Blocks.Num());
    }
    Shapes=NewCount; Revisions.Add(P.Cell,P.SnapshotSequence); Stages.Remove(P.Cell); return true;
}
