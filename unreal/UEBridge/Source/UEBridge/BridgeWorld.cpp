#include "BridgeWorld.h"
#include "BridgeBlockPreview.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"

ABridgeWorld::ABridgeWorld() { PrimaryActorTick.bCanEverTick=true; RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); }
bool ABridgeWorld::Inside(const FIntVector& C) const {
    return Scoped && FMath::Abs(C.X-Center.X)<=Radius && FMath::Abs(C.Y-Center.Y)<=HalfHeight && FMath::Abs(C.Z-Center.Z)<=Radius;
}
void ABridgeWorld::Clear(uint64 Barrier) {
    for (auto& Pair:Cells) if (IsValid(Pair.Value)) Pair.Value->Destroy();
    for(auto& Box:Boundary) if(Box) Box->DestroyComponent(); Boundary.Empty();
    Sealed=false; ImportId.Empty(); Stored.Empty();
    Cells.Empty(); Counts.Empty(); Revisions.Empty(); Stages.Empty(); Shapes=0; Scoped=false; ScopeSequence=0; ClearBarrier=Barrier;
}
void ABridgeWorld::EndPlay(const EEndPlayReason::Type Reason) { Clear(); Super::EndPlay(Reason); }
void ABridgeWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds); const double Now=FPlatformTime::Seconds();
    for (auto It=Stages.CreateIterator();It;++It) if (Now>It.Value().Deadline) It.RemoveCurrent();
}
bool ABridgeWorld::Handle(const FBridgePacket& P,const FVector& Anchor,UMaterialInterface* Material,UBridgeBlockPalette* Palette) {
    if(Sealed) return true; // ACK stale source updates without overwriting UE-owned geometry.
    if(IsImporting() && P.Kind==EBridgeKind::WorldClear) return true;
    if (P.Kind==EBridgeKind::WorldClear) { if (P.Sequence>=FMath::Max(ScopeSequence,ClearBarrier)) Clear(P.Sequence); return true; }
    if (P.Kind==EBridgeKind::WorldScope) {
        if (P.Sequence<=FMath::Max(ScopeSequence,ClearBarrier)) return true;
        Center=P.Cell; Radius=P.Radius; HalfHeight=P.HalfHeight; ScopeSequence=P.Sequence; Scoped=true;
        for (auto It=Cells.CreateIterator();It;++It) if (!Inside(It.Key())) {
            if (IsValid(It.Value())) It.Value()->Destroy(); Shapes-=Counts.FindRef(It.Key()); Counts.Remove(It.Key()); It.RemoveCurrent();
        }
        for (auto It=Revisions.CreateIterator();It;++It) if (!Inside(It.Key())) It.RemoveCurrent();
        // A sender completes its current cell before publishing a new scope.
        Stages.Empty(); if(IsImporting()) BuildBoundary(); return true;
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
    TArray<FBridgeBlock> Blocks; TSet<int32> Colors; TSet<FString> Groups;
    for (int32 I=0;I<Stage->Total;++I) { const auto* Batch=Stage->Batches.Find(I); if (!Batch) return false; Blocks.Append(*Batch); }
    for (const auto& Block:Blocks) { Colors.Add(Block.Color); Groups.Add(FString::Printf(TEXT("%s#%d#%d"),*Block.BlockId,Block.Color,Block.Collision)); }
    const int32 NewCount=Shapes-Counts.FindRef(P.Cell)+Blocks.Num();
    if (Blocks.Num()>8192 || Colors.Num()>64 || Groups.Num()>256 || NewCount>131072) return false;
    if (Blocks.IsEmpty()) {
        if (auto* Existing=Cells.Find(P.Cell)) { if (IsValid(*Existing)) (*Existing)->Destroy(); Cells.Remove(P.Cell); }
        Counts.Remove(P.Cell);
    } else {
        auto& Cell=Cells.FindOrAdd(P.Cell);
        if (!IsValid(Cell)) Cell=GetWorld()->SpawnActor<ABridgeBlockPreview>();
        if (!Cell) return false;
        Cell->Replace(Blocks,Anchor,Material,Palette,IsImporting()); Counts.Add(P.Cell,Blocks.Num());
    }
    if(IsImporting()) { Stored.Add(P.Cell,Blocks); SavedMaterial=Material; SavedPalette=Palette; }
    Shapes=NewCount; Revisions.Add(P.Cell,P.SnapshotSequence); Stages.Remove(P.Cell); return true;
}

bool ABridgeWorld::BeginImport(const FBridgePacket& P,const FVector& Anchor) {
    if(P.ImportId==ImportId) return true; // Retrying the same handshake must never clear received cells.
    if(P.Sequence<=ClearBarrier) return true;
    Clear(P.Sequence); ImportId=P.ImportId; ImportOrigin=P.MinecraftOrigin; ImportAnchor=Anchor; return true;
}
bool ABridgeWorld::CommitImport(const FBridgePacket& P) {
    if(P.ImportId!=ImportId || !Scoped) return false;
    const int32 Expected=(Radius*2+1)*(Radius*2+1)*(HalfHeight*2+1);
    if(P.ImportCells!=Expected || Revisions.Num()!=Expected || !Stages.IsEmpty()) return false;
    Sealed=true; return true;
}
void ABridgeWorld::BuildBoundary() {
    for(auto& Box:Boundary) if(Box) Box->DestroyComponent(); Boundary.Empty();
    const FVector Low((Center.X-Radius)*8,(Center.Y-HalfHeight)*8,(Center.Z-Radius)*8);
    const FVector High((Center.X+Radius+1)*8,(Center.Y+HalfHeight+1)*8,(Center.Z+Radius+1)*8);
    const FVector Middle=BridgeProtocol::ToUnreal((Low+High)*.5-ImportOrigin,ImportAnchor);
    const FVector Span=High-Low; const FVector Half(Span.Z*50,Span.X*50,Span.Y*50);
    // Invisible perimeter prevents falling into unimported space. Terrain itself supplies interior collision.
    for(int32 Axis=0;Axis<3;++Axis) for(int32 Side : {-1,1}) {
        auto* Box=NewObject<UBoxComponent>(this); Box->SetupAttachment(RootComponent);
        FVector Extent=Half; Extent[Axis]=10; Box->SetBoxExtent(Extent);
        FVector Location=Middle; Location[Axis]+=Side*(Half[Axis]+10);
        Box->SetWorldLocation(Location); Box->SetCollisionProfileName(TEXT("BlockAll"));
        Box->SetHiddenInGame(true); Box->SetCanEverAffectNavigation(false); Box->RegisterComponent(); Boundary.Add(Box);
    }
}
int32 ABridgeWorld::RemoveBlocksInSphere(FVector Position,float RemovalRadius) {
    if(!Sealed || !FMath::IsFinite(RemovalRadius) || RemovalRadius<=0 || RemovalRadius>5000) return 0;
    int32 Removed=0;
    for(auto& Pair:Stored) {
        const int32 Before=Pair.Value.Num();
        Pair.Value.RemoveAll([&](const FBridgeBlock& B){ return FVector::DistSquared(BridgeProtocol::ToUnreal(B.Position,ImportAnchor),Position)<=RemovalRadius*RemovalRadius; });
        const int32 Difference=Before-Pair.Value.Num(); if(!Difference) continue;
        Removed+=Difference; Shapes-=Difference;
        if(auto* Cell=Cells.Find(Pair.Key)) if(IsValid(*Cell)) (*Cell)->Replace(Pair.Value,ImportAnchor,SavedMaterial,SavedPalette,true);
        Counts.Add(Pair.Key,Pair.Value.Num());
    }
    return Removed;
}

namespace {
FIntVector CellOf(const FIntVector& Block) {
    return FIntVector(FMath::FloorToInt(Block.X/8.0),FMath::FloorToInt(Block.Y/8.0),FMath::FloorToInt(Block.Z/8.0));
}
}
FIntVector ABridgeWorld::OwnerOf(const FBridgeBlock& Block) const {
    if(Block.HasSourceBlock) return Block.SourceBlock;
    const FVector Absolute=Block.Position+ImportOrigin;
    return FIntVector(FMath::FloorToInt(Absolute.X),FMath::FloorToInt(Absolute.Y),FMath::FloorToInt(Absolute.Z));
}
FVector ABridgeWorld::BlockCenter(const FIntVector& Block) const {
    return BridgeProtocol::ToUnreal(FVector(Block)+FVector(.5)-ImportOrigin,ImportAnchor);
}
bool ABridgeWorld::Aim(const FVector& Start,const FRotator& Rotation,float Reach,FIntVector& Block,FVector& Normal,const AActor* Ignored) const {
    if(!Sealed) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgeAim),false);
    if(Ignored) Params.AddIgnoredActor(Ignored);
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Rotation.Vector()*Reach,ECC_Visibility,Params)) return false;
    const auto* PreviewActor=Cast<ABridgeBlockPreview>(Hit.GetActor());
    FBridgeBlock Shape;
    if(!PreviewActor || !PreviewActor->ResolveHit(Hit.GetComponent(),Hit.Item,Shape)) return false;
    const FIntVector SourceVoxel=OwnerOf(Shape); const auto* Cell=Cells.Find(CellOf(SourceVoxel));
    if(!Cell || Cell->Get()!=PreviewActor) return false;
    Block=SourceVoxel;Normal=Hit.ImpactNormal; return true;
}
void ABridgeWorld::RebuildCell(const FIntVector& CellKey) {
    auto& Actor=Cells.FindOrAdd(CellKey);
    if(!IsValid(Actor)) Actor=GetWorld()->SpawnActor<ABridgeBlockPreview>();
    if(Actor) Actor->Replace(Stored.FindChecked(CellKey),ImportAnchor,SavedMaterial,SavedPalette,true);
    Counts.Add(CellKey,Stored.FindChecked(CellKey).Num());
}
bool ABridgeWorld::BreakBlock(const FIntVector& Block) {
    if(!Sealed) return false;
    const FIntVector CellKey=CellOf(Block);auto* Data=Stored.Find(CellKey); if(!Data) return false;
    const int32 Count=Data->RemoveAll([&](const FBridgeBlock& Shape){return OwnerOf(Shape)==Block;});
    if(!Count) return false;
    Shapes-=Count;RebuildCell(CellKey);return true;
}
FString ABridgeWorld::PlaceBlock(const FIntVector& Block,const FString& BlockId,int32 Color) {
    const FIntVector CellKey=CellOf(Block);
    if(!Sealed || !Inside(CellKey) || !Revisions.Contains(CellKey)) return TEXT("outside import");
    auto* Data=Stored.Find(CellKey);if(!Data) return TEXT("cell unavailable");
    for(const auto& Shape:*Data) if(OwnerOf(Shape)==Block) return TEXT("occupied");
    if(Data->Num()>=8192 || Shapes>=131072) return TEXT("shape limit");
    TSet<int32> Colors;TSet<FString> Groups;
    for(const auto& Shape:*Data) {Colors.Add(Shape.Color);Groups.Add(FString::Printf(TEXT("%s#%d#%d"),*Shape.BlockId,Shape.Color,Shape.Collision));}
    Colors.Add(Color);Groups.Add(FString::Printf(TEXT("%s#%d#1"),*BlockId,Color));
    if(Colors.Num()>64 || Groups.Num()>256) return TEXT("material limit");
    // A small inset avoids rejecting a face shared with the supporting block.
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgePlace),false);
    if(GetWorld()->OverlapBlockingTestByChannel(BlockCenter(Block),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(FVector(49.9)),Params)) return TEXT("blocked by body or geometry");
    FBridgeBlock Shape;Shape.Position=FVector(Block)+FVector(.5)-ImportOrigin;
    Shape.BlockId=BlockId;Shape.Color=Color;Shape.Collision=true;Shape.SourceBlock=Block;Shape.HasSourceBlock=true;
    Data->Add(Shape);++Shapes;RebuildCell(CellKey);return TEXT("placed");
}
