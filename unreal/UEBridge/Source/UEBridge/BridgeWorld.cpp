#include "BridgeWorld.h"
#include "BridgeBlockPreview.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "BridgeBlockPalette.h"
#include "BridgeLightingService.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
namespace { TMap<FString,FString> StateProperties(const FString& State); }

ABridgeWorld::ABridgeWorld() { PrimaryActorTick.bCanEverTick=true; RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); }
bool ABridgeWorld::Inside(const FIntVector& C) const {
    return Scoped && FMath::Abs(C.X-Center.X)<=Radius && FMath::Abs(C.Y-Center.Y)<=HalfHeight && FMath::Abs(C.Z-Center.Z)<=Radius;
}
void ABridgeWorld::Clear(uint64 Barrier) {
    for (auto& Pair:Cells) if (IsValid(Pair.Value)) { Pair.Value->Clear(); Pair.Value->Destroy(); }
    for(auto& Box:Boundary) if(Box) Box->DestroyComponent(); Boundary.Empty();
    Sealed=false;ImportId.Empty();Stored.Empty();ButtonRelease.Empty();LastModelError.Empty();SurfaceReason=TEXT("not_sampled");
    Cells.Empty(); Counts.Empty(); Revisions.Empty(); Stages.Empty(); Shapes=0; Scoped=false; ScopeSequence=0; ClearBarrier=Barrier;
    OpaqueCells.Empty();RebuildQueue.Empty();LightQueue.Empty();EditedBlocks.Empty();EditedCellOwners.Empty();RemovedBlocks.Empty();SkyTops.Empty();PhysicsCells.Empty();AdditionalCollisionPositions.Empty();HasCollisionCenter=false;
    if(Lighting) Lighting->Clear();Lighting.Reset();PendingLighting.Reset();LightSeedCells.Empty();LightSeedCursor=0;PendingLightInitialized=false;LightingRadius=LightingHeight=0;
}
void ABridgeWorld::EndPlay(const EEndPlayReason::Type Reason) { Clear(); Super::EndPlay(Reason); }
void ABridgeWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds); const double Now=FPlatformTime::Seconds();
    for (auto It=Stages.CreateIterator();It;++It) if (Now>It.Value().Deadline) It.RemoveCurrent();
    TArray<FIntVector> Released;
    for(const auto& Pair:ButtonRelease) if(Now>=Pair.Value) Released.Add(Pair.Key);
    for(const auto& Block:Released) {ButtonRelease.Remove(Block);const auto* Visual=FindVisual(Block);
        if(Visual && StateProperties(Visual->StateKey).FindRef(TEXT("powered"))==TEXT("true")) UseBlock(Block,true);}
    if(Lighting && Sealed) {Lighting->Tick(100000);LightQueue.Append(Lighting->ConsumeChangedCells());}
    if(Sealed && !PendingLighting && (LightingCenter!=Center || LightingRadius!=Radius || LightingHeight!=HalfHeight)) BeginLightingRecenter();
    if(PendingLighting) {
        const double SeedDeadline=FPlatformTime::Seconds()+.004;
        int32 SeededRows=0;
        for(int32 Work=0;Work<2048 && SeededRows<32768 && LightSeedCursor<LightSeedCells.Num() && FPlatformTime::Seconds()<SeedDeadline;++Work) {
            const FIntVector Cell=LightSeedCells[LightSeedCursor++];const auto* Rows=Stored.Find(Cell);if(Rows) SeededRows+=Rows->Num();
            SeedLightingCell(Cell,PendingLighting.Get());
        }
        if(LightSeedCursor==LightSeedCells.Num() && !PendingLightInitialized) {PendingLighting->BeginInitialize();PendingLightInitialized=true;}
        if(PendingLightInitialized) {
            PendingLighting->Tick(200000);
            // Relight only the affected terrain after the new bounded field has settled.
            PendingLighting->DiscardChanged();
            if(PendingLighting->Pending()==0) {
                Lighting=PendingLighting;PendingLighting.Reset();LightSeedCells.Empty();
                LightingCenter=PendingLightingCenter;LightingRadius=PendingLightingRadius;LightingHeight=PendingLightingHeight;
                for(const auto& Pair:Cells) LightQueue.Add(Pair.Key);
            }
        }
    }
    // Native cells are rebuilt locally, spread across frames; collision changes from edits are immediate.
    const double RebuildDeadline=FPlatformTime::Seconds()+.004;
    for(int32 Work=0;Work<4 && !RebuildQueue.IsEmpty() && FPlatformTime::Seconds()<RebuildDeadline;++Work) {
        const FIntVector Cell=*RebuildQueue.CreateConstIterator();RebuildQueue.Remove(Cell);
        if(Stored.Contains(Cell) && Inside(Cell)) RebuildCell(Cell);
    }
    for(int32 Work=0;Work<8 && !LightQueue.IsEmpty() && FPlatformTime::Seconds()<RebuildDeadline;++Work) {
        const FIntVector Cell=*LightQueue.CreateConstIterator();LightQueue.Remove(Cell);
        if(auto* Actor=Cells.Find(Cell)) if(IsValid(*Actor)) (*Actor)->Relight(Lighting.Get());
    }
}
bool ABridgeWorld::Handle(const FBridgePacket& P,const FVector& Anchor,UMaterialInterface* Material,UBridgeBlockPalette* Palette) {
    if(Sealed && P.Kind!=EBridgeKind::WorldCell && P.Kind!=EBridgeKind::WorldScope) return true;
    if(IsImporting() && P.Kind==EBridgeKind::WorldClear) return true;
    if (P.Kind==EBridgeKind::WorldClear) { if (P.Sequence>=FMath::Max(ScopeSequence,ClearBarrier)) Clear(P.Sequence); return true; }
    if (P.Kind==EBridgeKind::WorldScope) {
        if (P.Sequence<=FMath::Max(ScopeSequence,ClearBarrier)) return true;
        Center=P.Cell; Radius=P.Radius; HalfHeight=P.HalfHeight; ScopeSequence=P.Sequence; Scoped=true;
        for (auto It=Cells.CreateIterator();It;++It) if (!Inside(It.Key())) {
            if (IsValid(It.Value())) { It.Value()->Clear(); It.Value()->Destroy(); } Shapes-=Counts.FindRef(It.Key()); Counts.Remove(It.Key()); It.RemoveCurrent();
        }
        for (auto It=Revisions.CreateIterator();It;++It) if (!Inside(It.Key())) It.RemoveCurrent();
        for(auto It=Stored.CreateIterator();It;++It) if(!Inside(It.Key())) {
            OpaqueCells.Remove(It.Key());for(const auto& Block:It.Value()) {if(Lighting) Lighting->ClearVoxel(OwnerOf(Block));if(PendingLighting) PendingLighting->ClearVoxel(OwnerOf(Block));}
            Shapes-=Counts.FindRef(It.Key());Counts.Remove(It.Key());SkyTops.Remove(It.Key());It.RemoveCurrent();
        }
        if(!HasCollisionCenter) {CollisionCenter=Center;HasCollisionCenter=true;}
        if(!Lighting) {
            const FIntVector Low((Center.X-Radius)*8-1,(Center.Y-HalfHeight)*8-1,(Center.Z-Radius)*8-1);
            const FIntVector High((Center.X+Radius+1)*8,(Center.Y+HalfHeight+1)*8,(Center.Z+Radius+1)*8);
            Lighting=MakeShared<FBridgeLightingService>();if(!Lighting->Reset(Low,High)) {LastModelError=TEXT("lighting region exceeds memory budget");return false;}
            LightingCenter=Center;LightingRadius=Radius;LightingHeight=HalfHeight;
        } else if(Sealed && !PendingLighting) BeginLightingRecenter();
        // A sender completes its current cell before publishing a new scope.
        Stages.Empty(); if(!ImportId.IsEmpty()) BuildBoundary(); return true;
    }
    if (P.Kind!=EBridgeKind::WorldCell) return false;
    if(P.CompactTerrain && !P.MinecraftOrigin.Equals(ImportOrigin,.000001)) return false;
    if (!Inside(P.Cell) || P.SnapshotSequence<ScopeSequence || P.SnapshotSequence<=ClearBarrier
        || P.SnapshotSequence<=Revisions.FindRef(P.Cell)) return true;
    FBridgeWorldStage* Stage=Stages.Find(P.Cell);
    if (!Stage || P.SnapshotSequence>Stage->Sequence) {
        if (!Stage && Stages.Num()>=16) return false;
        FBridgeWorldStage Next; Next.Sequence=P.SnapshotSequence; Next.Id=P.SnapshotId; Next.Total=P.TotalBatches;
        Next.Deadline=FPlatformTime::Seconds()+60;Next.Compact=P.CompactTerrain;Stages.Add(P.Cell,MoveTemp(Next));Stage=Stages.Find(P.Cell);
    }
    if (P.SnapshotSequence<Stage->Sequence) return true;
    if (Stage->Id!=P.SnapshotId || Stage->Total!=P.TotalBatches || Stage->Compact!=P.CompactTerrain) return false;
    if(P.SkyTop.Num()==64) Stage->SkyTop=P.SkyTop;
    if (!Stage->Batches.Contains(P.BatchIndex)) Stage->Batches.Add(P.BatchIndex,P.Blocks);
    if (Stage->Batches.Num()!=Stage->Total) return true;
    TArray<FBridgeBlock> Blocks; TSet<int32> Colors; TSet<FString> Groups;
    for (int32 I=0;I<Stage->Total;++I) { const auto* Batch=Stage->Batches.Find(I); if (!Batch) return false; Blocks.Append(*Batch); }
    if(Stage->Compact) {
        if(Blocks.Num()>512) return false;TSet<FIntVector> Owners;
        for(const auto& Block:Blocks) {if(!Block.NativeCompact || Owners.Contains(Block.SourceBlock)) return false;Owners.Add(Block.SourceBlock);}
    }
    for(auto& Block:Blocks) if(Block.NativeCompact && Palette) Block.Position+=Palette->GetModelOffset(Block.BlockId,Block.SourceBlock);
    // Minecraft remains a source snapshot. UE edits, including deleted voxels, survive streaming away/back.
    Blocks.RemoveAll([&](const FBridgeBlock& Block){const FIntVector BlockOwner=OwnerOf(Block);return RemovedBlocks.Contains(BlockOwner) || EditedBlocks.Contains(BlockOwner);});
    if(const auto* Owners=EditedCellOwners.Find(P.Cell)) for(const auto& BlockOwner:*Owners) if(const auto* Edited=EditedBlocks.Find(BlockOwner)) Blocks.Append(*Edited);
    for (const auto& Block:Blocks) { Colors.Add(Block.Color); Groups.Add(FString::Printf(TEXT("%s#%d#%d"),*Block.BlockId,Block.Color,Block.Collision)); }
    TSet<FString> ValidatedModels;
    for(const auto& Block:Blocks) if(Block.Role==1) {
        const FString ModelKey=Block.BlockId+TEXT("[")+Block.StateKey+TEXT("]");if(ValidatedModels.Contains(ModelKey)) continue;ValidatedModels.Add(ModelKey);
        TArray<FBridgeModelFace> Faces;TArray<FBox> Collision,Outline;
        if(!Palette || !Palette->GetStateBoxes(Block.BlockId,Block.StateKey,Collision,Outline) || !Palette->BuildModel(Block.BlockId,Block.StateKey,Faces)) {
            LastModelError=TEXT("model unavailable: ")+Block.BlockId+TEXT("[")+Block.StateKey+TEXT("]");return false;
        }
        for(const auto& Face:Faces) if(!Palette->FindFaceMaterial(Face.TextureId,Face.bTint)) {
            LastModelError=TEXT("face material unavailable: ")+Face.TextureId;return false;
        }
    }
    LastModelError.Empty();
    const int32 NewCount=Shapes-Counts.FindRef(P.Cell)+Blocks.Num();
    if (Blocks.Num()>8192 || Colors.Num()>512 || Groups.Num()>2048 || NewCount>4194304) return false;
    if (Blocks.IsEmpty()) {
        if (auto* Existing=Cells.Find(P.Cell)) { if (IsValid(*Existing)) { (*Existing)->Clear(); (*Existing)->Destroy(); } Cells.Remove(P.Cell); }
        Counts.Remove(P.Cell);
    } else Counts.Add(P.Cell,Blocks.Num());
    // Logical data is stored even when every rendered face is currently occluded.
    if(IsImporting() || Sealed) {
        if(const auto* Previous=Stored.Find(P.Cell)) for(const auto& Block:*Previous) {if(Lighting) Lighting->ClearVoxel(OwnerOf(Block));if(PendingLighting) PendingLighting->ClearVoxel(OwnerOf(Block));}
        Stored.Add(P.Cell,Blocks);SavedMaterial=Material;SavedPalette=Palette;RefreshLogicalCell(P.Cell);
        if(Stage->SkyTop.Num()==64) SkyTops.Add(P.Cell,Stage->SkyTop);
        if(const auto* Tops=SkyTops.Find(P.Cell)) if(Tops->Num()==64) {
            for(int32 Z=0;Z<8;++Z) for(int32 X=0;X<8;++X) {if(Lighting && P.Cell.Y==LightingCenter.Y+LightingHeight) Lighting->SetSkyBoundary(P.Cell.X*8+X,P.Cell.Z*8+Z,(*Tops)[X+(Z<<3)]);
                if(PendingLighting && P.Cell.Y==PendingLightingCenter.Y+PendingLightingHeight) PendingLighting->SetSkyBoundary(P.Cell.X*8+X,P.Cell.Z*8+Z,(*Tops)[X+(Z<<3)]);}
        }
        QueueNeighbors(P.Cell);
        // Near cells get collision before the source ACK; far rendering is frame-budgeted.
        if(NearCollision(P.Cell)) {RebuildCell(P.Cell);RebuildQueue.Remove(P.Cell);}
    } else if(!Blocks.IsEmpty()) {
        auto& Cell=Cells.FindOrAdd(P.Cell);if(!IsValid(Cell)) Cell=GetWorld()->SpawnActor<ABridgeBlockPreview>();
        if(!Cell) return false;Cell->Replace(Blocks,Anchor,Material,Palette,false);
    }
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
    Sealed=true;if(Lighting) Lighting->BeginInitialize();return true;
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
        TSet<FIntVector> Targets;for(const auto& B:Pair.Value) if(FVector::DistSquared(BlockCenter(OwnerOf(B)),Position)<=RemovalRadius*RemovalRadius) Targets.Add(OwnerOf(B));
        Pair.Value.RemoveAll([&](const FBridgeBlock& B){ return Targets.Contains(OwnerOf(B)); });
        const int32 Difference=Before-Pair.Value.Num(); if(!Difference) continue;
        Removed+=Difference; Shapes-=Difference;
        for(const auto& Target:Targets) MarkEdited(Target);
        // Refresh below also exposes neighbor faces across cell borders.
        Counts.Add(Pair.Key,Pair.Value.Num());RefreshLogicalCell(Pair.Key);QueueNeighbors(Pair.Key);RebuildCell(Pair.Key);
    }
    return Removed;
}

namespace {
FIntVector CellOf(const FIntVector& Block) {
    return FIntVector(FMath::FloorToInt(Block.X/8.0),FMath::FloorToInt(Block.Y/8.0),FMath::FloorToInt(Block.Z/8.0));
}
TMap<FString,FString> StateProperties(const FString& State) {
    TMap<FString,FString> Result; TArray<FString> Parts; State.ParseIntoArray(Parts,TEXT(","),true);
    for(const auto& Part:Parts) { FString Key,Value; if(Part.Split(TEXT("="),&Key,&Value)) Result.Add(Key,Value); }
    return Result;
}
FString StateKey(const TMap<FString,FString>& Properties) {
    TArray<FString> Keys; Properties.GetKeys(Keys); Keys.Sort(); FString Result;
    for(const auto& Key:Keys) { if(!Result.IsEmpty()) Result+=TEXT(","); Result+=Key+TEXT("=")+Properties.FindChecked(Key); } return Result;
}
FString FacingForYaw(double Yaw) {
    static const TCHAR* Directions[]={TEXT("south"),TEXT("west"),TEXT("north"),TEXT("east")};
    return Directions[(FMath::RoundToInt(Yaw/90.0)%4+4)%4];
}
FString Opposite(const FString& Direction) {
    if(Direction==TEXT("north")) return TEXT("south"); if(Direction==TEXT("south")) return TEXT("north");
    if(Direction==TEXT("east")) return TEXT("west"); return TEXT("east");
}
FIntVector Offset(const FString& Direction) {
    if(Direction==TEXT("north")) return FIntVector(0,0,-1); if(Direction==TEXT("south")) return FIntVector(0,0,1);
    if(Direction==TEXT("east")) return FIntVector(1,0,0); return FIntVector(-1,0,0);
}
FString LeftOf(const FString& Direction) {
    if(Direction==TEXT("north")) return TEXT("west"); if(Direction==TEXT("south")) return TEXT("east");
    if(Direction==TEXT("east")) return TEXT("north"); return TEXT("south");
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
bool ABridgeWorld::GetBlockInfo(const FIntVector& SourceVoxel,FString& BlockId,FColor& Tint) const {
    if(!Sealed) return false;
    const auto* Data=Stored.Find(CellOf(SourceVoxel)); if(!Data) return false;
    for(const auto& Shape:*Data) if(OwnerOf(Shape)==SourceVoxel) {
        BlockId=Shape.BlockId;
        Tint=FColor((Shape.Color>>16)&255,(Shape.Color>>8)&255,Shape.Color&255);
        return !BlockId.IsEmpty();
    }
    return false;
}
bool ABridgeWorld::GetBlockOutline(const FIntVector& SourceVoxel,TArray<FBox>& MinecraftBoxes) const {
    MinecraftBoxes.Empty();
    const auto* Data=Stored.Find(CellOf(SourceVoxel)); if(!Data) return false;
    bool SeparateOutline=false;for(const auto& Shape:*Data) if(OwnerOf(Shape)==SourceVoxel && Shape.Role==3) SeparateOutline=true;
    for(const auto& Shape:*Data) if(OwnerOf(Shape)==SourceVoxel && (Shape.Role==3 || Shape.Role==0 || (!SeparateOutline && Shape.Role==2))) {
        const FVector Local=Shape.Position+ImportOrigin-FVector(SourceVoxel);
        MinecraftBoxes.Add(FBox(Local-Shape.Size*.5,Local+Shape.Size*.5));
    }
    if(MinecraftBoxes.IsEmpty() && SavedPalette) if(const auto* Visual=FindVisual(SourceVoxel)) {
        TArray<FBox> Collision;if(SavedPalette->GetStateBoxes(Visual->BlockId,Visual->StateKey,Collision,MinecraftBoxes)) {
            const FVector Offset=SavedPalette->GetModelOffset(Visual->BlockId,SourceVoxel);for(auto& Box:MinecraftBoxes) Box=Box.ShiftBy(Offset);
        }
    }
    return !MinecraftBoxes.IsEmpty();
}
bool ABridgeWorld::GetBlockState(const FIntVector& SourceVoxel,FString& BlockId,FString& Key) const {
    const auto* Visual=FindVisual(SourceVoxel);if(!Visual) return false;
    BlockId=Visual->BlockId;Key=Visual->StateKey;return true;
}
bool ABridgeWorld::GetSupportingBlock(const FVector& Feet,FIntVector& SourceVoxel,FString& BlockId,FColor& Tint,FVector& ImpactPoint,const AActor* Ignored) const {
    if(!Sealed || !GetWorld()) {SurfaceReason=TEXT("world_not_ready");return false;}
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgeSurface),false);
    if(Ignored) Params.AddIgnoredActor(Ignored);
    FHitResult Hit;
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Feet+FVector(0,0,8),Feet-FVector(0,0,35),ECC_Pawn,Params)) {SurfaceReason=TEXT("no_floor_hit");return false;}
    const auto* PreviewActor=Cast<ABridgeBlockPreview>(Hit.GetActor());
    FBridgeBlock Shape;
    if(!PreviewActor || !PreviewActor->ResolveHit(Hit.GetComponent(),Hit.Item,Shape)) {
        const FString LevelActor=Hit.GetActor() ? Hit.GetActor()->GetName().Left(96) : TEXT("unknown");
        if(SupportingLogical(Feet,SourceVoxel,BlockId,Tint,ImpactPoint)) {SurfaceReason=TEXT("native_surface_fallback; level collider=")+LevelActor;return true;}
        SurfaceReason=TEXT("non_bridge_floor; level collider=")+LevelActor;return false;
    }
    if(Shape.Role==4) {const FVector MCPoint=FVector(-Hit.ImpactPoint.Y+ImportAnchor.Y,Hit.ImpactPoint.Z-ImportAnchor.Z,Hit.ImpactPoint.X-ImportAnchor.X)/100+ImportOrigin;
        const FVector MCNormal(-Hit.ImpactNormal.Y,Hit.ImpactNormal.Z,Hit.ImpactNormal.X);const FVector Inside=MCPoint-MCNormal*.001;
        SourceVoxel=FIntVector(FMath::FloorToInt(Inside.X),FMath::FloorToInt(Inside.Y),FMath::FloorToInt(Inside.Z));}
    else SourceVoxel=OwnerOf(Shape);
    if(!GetBlockInfo(SourceVoxel,BlockId,Tint)) {SurfaceReason=TEXT("native_surface_metadata_missing");return false;}
    ImpactPoint=Hit.ImpactPoint;SurfaceReason=TEXT("native_surface");return true;
}
bool ABridgeWorld::SupportingLogical(const FVector& Feet,FIntVector& Voxel,FString& BlockId,FColor& Tint,FVector& Point) const {
    const FVector Absolute=FVector(-Feet.Y+ImportAnchor.Y,Feet.Z-ImportAnchor.Z,Feet.X-ImportAnchor.X)/100+ImportOrigin;
    const FIntVector At(FMath::FloorToInt(Absolute.X),FMath::FloorToInt(Absolute.Y),FMath::FloorToInt(Absolute.Z));
    double Best=.050001;bool Found=false;
    for(int32 Y=0;Y>=-4;--Y) for(int32 X=-1;X<=1;++X) for(int32 Z=-1;Z<=1;++Z) {
        const FIntVector Candidate=At+FIntVector(X,Y,Z);if(!NearCollision(CellOf(Candidate)) || !Stored.Contains(CellOf(Candidate))) continue;
        const auto* Visual=FindVisual(Candidate);if(!Visual) continue;TArray<FBox> Collision,Outline;FVector Offset=FVector::ZeroVector;
        if(Visual->Role==1) {if(!SavedPalette || !SavedPalette->GetStateBoxes(Visual->BlockId,Visual->StateKey,Collision,Outline)) continue;Offset=SavedPalette->GetModelOffset(Visual->BlockId,Candidate);}
        else if(Visual->Collision) {const FVector ShapeCenter=Visual->Position+ImportOrigin-FVector(Candidate);Collision.Add(FBox(ShapeCenter-Visual->Size*.5,ShapeCenter+Visual->Size*.5));}
        for(const auto& Box:Collision) {
            const FVector Low=FVector(Candidate)+Offset+Box.Min,High=FVector(Candidate)+Offset+Box.Max;
            const double Gap=FMath::Abs(Absolute.Y-High.Y);
            if(Gap>=Best || Absolute.X<Low.X-.001 || Absolute.X>High.X+.001 || Absolute.Z<Low.Z-.001 || Absolute.Z>High.Z+.001) continue;
            if(!GetBlockInfo(Candidate,BlockId,Tint)) continue;Best=Gap;Voxel=Candidate;
            Point=BridgeProtocol::ToUnreal(FVector(Absolute.X,High.Y,Absolute.Z)-ImportOrigin,ImportAnchor);Found=true;
        }
    }
    return Found;
}
bool ABridgeWorld::Aim(const FVector& Start,const FRotator& Rotation,float Reach,FIntVector& Block,FVector& Normal,const AActor* Ignored,FVector* HitPoint) const {
    if(!Sealed) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgeAim),false);
    if(Ignored) Params.AddIgnoredActor(Ignored);
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Rotation.Vector()*Reach,ECC_Visibility,Params)) return false;
    if(!Cast<ABridgeBlockPreview>(Hit.GetActor())) {
        const FHitResult First=Hit;bool Native=false;
        // Saved editor floors can coincide with an imported face. Resolve only a
        // matching native surface; never select through a separate saved wall.
        for(int32 Retry=0;Retry<4 && Hit.GetActor();++Retry) {
            Params.AddIgnoredActor(Hit.GetActor());
            if(!GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Rotation.Vector()*Reach,ECC_Visibility,Params)) break;
            if(FVector::DistSquared(First.ImpactPoint,Hit.ImpactPoint)>25 || FVector::DotProduct(First.ImpactNormal,Hit.ImpactNormal)<.95) break;
            if(Cast<ABridgeBlockPreview>(Hit.GetActor())) {Native=true;break;}
        }
        if(!Native) return false;
    }
    const auto* PreviewActor=Cast<ABridgeBlockPreview>(Hit.GetActor());
    FBridgeBlock Shape;
    if(!PreviewActor || !PreviewActor->ResolveHit(Hit.GetComponent(),Hit.Item,Shape)) return false;
    FIntVector SourceVoxel=OwnerOf(Shape);
    if(Shape.Role==4) {const FVector MCPoint=FVector(-Hit.ImpactPoint.Y+ImportAnchor.Y,Hit.ImpactPoint.Z-ImportAnchor.Z,Hit.ImpactPoint.X-ImportAnchor.X)/100+ImportOrigin;
        const FVector MCNormal(-Hit.ImpactNormal.Y,Hit.ImpactNormal.Z,Hit.ImpactNormal.X);const FVector Inside=MCPoint-MCNormal*.001;
        SourceVoxel=FIntVector(FMath::FloorToInt(Inside.X),FMath::FloorToInt(Inside.Y),FMath::FloorToInt(Inside.Z));}
    const auto* Cell=Cells.Find(CellOf(SourceVoxel));
    if(!Cell || Cell->Get()!=PreviewActor) return false;
    Block=SourceVoxel;Normal=Hit.ImpactNormal; if(HitPoint) *HitPoint=Hit.ImpactPoint; return true;
}
void ABridgeWorld::RebuildCell(const FIntVector& CellKey) {
    if(!Stored.Contains(CellKey)) return;
    auto& Actor=Cells.FindOrAdd(CellKey);
    if(!IsValid(Actor)) Actor=GetWorld()->SpawnActor<ABridgeBlockPreview>();
    if(Actor) {
        Actor->Replace(Stored.FindChecked(CellKey),ImportAnchor,SavedMaterial,SavedPalette,NearCollision(CellKey),[this](const FIntVector& P){return IsOpaqueVoxel(P);},Lighting.Get());
        if(!Actor->HasContent()) {Actor->Clear();Actor->Destroy();Cells.Remove(CellKey);}
    }
    Counts.Add(CellKey,Stored.FindChecked(CellKey).Num());
}
bool ABridgeWorld::NearCollision(const FIntVector& Cell) const {
    if(!PhysicsCells.IsEmpty()) return PhysicsCells.Contains(Cell);
    const FIntVector Near=HasCollisionCenter ? CollisionCenter : Center;
    return FMath::Abs(Cell.X-Near.X)<=3 && FMath::Abs(Cell.Y-Near.Y)<=2 && FMath::Abs(Cell.Z-Near.Z)<=3;
}
bool ABridgeWorld::IsOpaqueVoxel(const FIntVector& Block) const {
    const FIntVector Cell=CellOf(Block);const auto* Mask=OpaqueCells.Find(Cell);if(!Mask) return false;
    const FIntVector Local=Block-Cell*8;const int32 Index=Local.X+(Local.Z<<3)+(Local.Y<<6);
    return (Mask->Words[Index>>6]&(uint64(1)<<(Index&63)))!=0;
}
void ABridgeWorld::ClearOpaqueVoxel(const FIntVector& Voxel) {
    const FIntVector Cell=CellOf(Voxel);if(auto* Mask=OpaqueCells.Find(Cell)) {
        const FIntVector Local=Voxel-Cell*8;const int32 Index=Local.X+(Local.Z<<3)+(Local.Y<<6);Mask->Words[Index>>6]&=~(uint64(1)<<(Index&63));
    }
}
bool ABridgeWorld::ContainsUEPosition(const FVector& P) const {
    if(P.ContainsNaN()) return false;
    const FVector MC(-P.Y+ImportAnchor.Y,P.Z-ImportAnchor.Z,P.X-ImportAnchor.X);const FVector Absolute=MC/100+ImportOrigin;
    const FIntVector Cell=CellOf(FIntVector(FMath::FloorToInt(Absolute.X),FMath::FloorToInt(Absolute.Y),FMath::FloorToInt(Absolute.Z)));
    return Inside(Cell) && Stored.Contains(Cell);
}
bool ABridgeWorld::IsMovementReady(const FVector& UEFeet,const FVector& Velocity) const {
    if(!Sealed || UEFeet.ContainsNaN() || Velocity.ContainsNaN()) return false;
    const FVector Ahead=UEFeet+Velocity.GetClampedToMaxSize(2500)*.15;
    for(const FVector& CenterPoint:{UEFeet,Ahead}) {
        // Capsule clearance plus the ground just below the feet must be loaded before movement.
        for(float X:{-70.f,70.f}) for(float Y:{-70.f,70.f}) for(float Z:{-100.f,200.f})
            if(!ContainsUEPosition(CenterPoint+FVector(X,Y,Z))) return false;
    }
    return true;
}
bool ABridgeWorld::EnsureCollisionForPosition(const FVector& UEPosition) {
    if(!ContainsUEPosition(UEPosition) || !ContainsUEPosition(UEPosition-FVector(0,0,100))) return false;
    TArray<FVector> Extra=AdditionalCollisionPositions;Extra.Add(UEPosition);UpdateCollisionCenters(PrimaryCollisionPosition,Extra);return true;
}
void ABridgeWorld::QueueNeighbors(const FIntVector& Cell) {
    RebuildQueue.Add(Cell);for(const FIntVector& Delta:{FIntVector(1,0,0),FIntVector(-1,0,0),FIntVector(0,1,0),FIntVector(0,-1,0),FIntVector(0,0,1),FIntVector(0,0,-1)})
        if(Stored.Contains(Cell+Delta)) RebuildQueue.Add(Cell+Delta);
}
void ABridgeWorld::RefreshLogicalCell(const FIntVector& Cell) {
    const auto* Rows=Stored.Find(Cell);if(!Rows) return;
    auto& Mask=OpaqueCells.FindOrAdd(Cell);FMemory::Memzero(Mask.Words,sizeof(Mask.Words));
    for(const auto& Block:*Rows) if(Block.Role==1 || Block.Role==0) {
        const FIntVector BlockOwner=OwnerOf(Block);
        if(SavedPalette && SavedPalette->IsOpaqueFullCube(Block.BlockId,Block.StateKey)) {
            const FIntVector Local=BlockOwner-Cell*8;const int32 Index=Local.X+(Local.Z<<3)+(Local.Y<<6);Mask.Words[Index>>6]|=uint64(1)<<(Index&63);
        }
    }
    SeedLightingCell(Cell,Lighting.Get());if(PendingLighting) SeedLightingCell(Cell,PendingLighting.Get());
}
void ABridgeWorld::SeedLightingCell(const FIntVector& Cell,FBridgeLightingService* Service) const {
    const auto* Rows=Stored.Find(Cell);if(!Service || !Rows) return;
    for(const auto& Block:*Rows) if(Block.Role==1 || Block.Role==0) {
        uint8 Opacity=Block.HasLight ? Block.Opacity : 15,Emission=Block.HasLight ? Block.Emission : 0;
        if(!Block.HasLight) FBridgeLightingService::StateProperties(SavedPalette,Block.BlockId,Block.StateKey,Opacity,Emission);
        Service->SetVoxel(OwnerOf(Block),Opacity,Emission,Block.SkyLight,Block.BlockLight,
            Opacity<15 ? FBridgeLightingService::StateFaceMask(SavedPalette,Block.BlockId,Block.StateKey) : 0);
    }
}
void ABridgeWorld::BeginLightingRecenter() {
    if(PendingLighting || !Scoped) return;
    const FIntVector Low((Center.X-Radius)*8-1,(Center.Y-HalfHeight)*8-1,(Center.Z-Radius)*8-1);
    const FIntVector High((Center.X+Radius+1)*8,(Center.Y+HalfHeight+1)*8,(Center.Z+Radius+1)*8);
    PendingLighting=MakeShared<FBridgeLightingService>();
    if(!PendingLighting->Reset(Low,High)) {PendingLighting.Reset();LastModelError=TEXT("lighting region exceeds memory budget");return;}
    PendingLightingCenter=Center;PendingLightingRadius=Radius;PendingLightingHeight=HalfHeight;
    Stored.GetKeys(LightSeedCells);LightSeedCursor=0;PendingLightInitialized=false;
    LightSeedCells.Sort([this](const FIntVector& A,const FIntVector& B){return FMath::Abs(A.X-CollisionCenter.X)+FMath::Abs(A.Y-CollisionCenter.Y)+FMath::Abs(A.Z-CollisionCenter.Z)
        <FMath::Abs(B.X-CollisionCenter.X)+FMath::Abs(B.Y-CollisionCenter.Y)+FMath::Abs(B.Z-CollisionCenter.Z);});
    for(const auto& Pair:SkyTops) if(Pair.Key.Y==PendingLightingCenter.Y+PendingLightingHeight && Pair.Value.Num()==64)
        for(int32 Z=0;Z<8;++Z) for(int32 X=0;X<8;++X) PendingLighting->SetSkyBoundary(Pair.Key.X*8+X,Pair.Key.Z*8+Z,Pair.Value[X+(Z<<3)]);
}
void ABridgeWorld::MarkEdited(const FIntVector& Block) {
    const auto* Rows=Stored.Find(CellOf(Block));TArray<FBridgeBlock> Edited;
    if(Rows) for(const auto& Row:*Rows) if(OwnerOf(Row)==Block) Edited.Add(Row);
    if(Edited.IsEmpty()) {EditedBlocks.Remove(Block);if(auto* Owners=EditedCellOwners.Find(CellOf(Block))) Owners->Remove(Block);RemovedBlocks.Add(Block);ClearOpaqueVoxel(Block);if(Lighting) Lighting->ClearVoxel(Block);if(PendingLighting) PendingLighting->ClearVoxel(Block);}
    else {RemovedBlocks.Remove(Block);EditedBlocks.Add(Block,MoveTemp(Edited));EditedCellOwners.FindOrAdd(CellOf(Block)).Add(Block);RefreshLogicalCell(CellOf(Block));}
    QueueNeighbors(CellOf(Block));
}
void ABridgeWorld::UpdateCollisionCenter(const FVector& UEFeet) {
    UpdateCollisionCenters(UEFeet,AdditionalCollisionPositions);
}
void ABridgeWorld::UpdateCollisionCenters(const FVector& UEFeet,const TArray<FVector>& ExtraFeet) {
    if(!Scoped) return;
    PrimaryCollisionPosition=UEFeet;
    const FVector MC(-UEFeet.Y+ImportAnchor.Y,UEFeet.Z-ImportAnchor.Z,UEFeet.X-ImportAnchor.X);const FVector Absolute=MC/100+ImportOrigin;
    const FIntVector Next=CellOf(FIntVector(FMath::FloorToInt(Absolute.X),FMath::FloorToInt(Absolute.Y),FMath::FloorToInt(Absolute.Z)));
    TSet<FIntVector> NextPhysics;
    for(int32 X=-3;X<=3;++X) for(int32 Y=-2;Y<=2;++Y) for(int32 Z=-3;Z<=3;++Z) NextPhysics.Add(Next+FIntVector(X,Y,Z));
    for(int32 I=0;I<FMath::Min(ExtraFeet.Num(),512);++I) {
        const FVector& P=ExtraFeet[I];if(P.ContainsNaN()) continue;
        const FVector AbsoluteExtra=FVector(-P.Y+ImportAnchor.Y,P.Z-ImportAnchor.Z,P.X-ImportAnchor.X)/100+ImportOrigin;
        const FIntVector C=CellOf(FIntVector(FMath::FloorToInt(AbsoluteExtra.X),FMath::FloorToInt(AbsoluteExtra.Y),FMath::FloorToInt(AbsoluteExtra.Z)));
        for(int32 X=-1;X<=1;++X) for(int32 Y=-2;Y<=2;++Y) for(int32 Z=-1;Z<=1;++Z) NextPhysics.Add(C+FIntVector(X,Y,Z));
    }
    if(PhysicsCells.Num()==NextPhysics.Num()) {bool Same=true;for(const auto& C:NextPhysics) if(!PhysicsCells.Contains(C)) {Same=false;break;}if(Same) return;}
    const TSet<FIntVector> PreviousPhysics=PhysicsCells;const FIntVector Before=CollisionCenter;const bool HadCenter=HasCollisionCenter;
    AdditionalCollisionPositions=ExtraFeet;PhysicsCells=MoveTemp(NextPhysics);CollisionCenter=Next;HasCollisionCenter=true;
    for(const auto& Pair:Stored) {
        const bool WasNear=PreviousPhysics.IsEmpty() ? HadCenter && FMath::Abs(Pair.Key.X-Before.X)<=3 && FMath::Abs(Pair.Key.Y-Before.Y)<=2 && FMath::Abs(Pair.Key.Z-Before.Z)<=3 : PreviousPhysics.Contains(Pair.Key);
        if(WasNear==NearCollision(Pair.Key)) continue;
        if(auto* Actor=Cells.Find(Pair.Key)) {if(IsValid(*Actor)) (*Actor)->Replace(Pair.Value,ImportAnchor,SavedMaterial,SavedPalette,NearCollision(Pair.Key),{},Lighting.Get(),false);}
        else if(NearCollision(Pair.Key)) RebuildCell(Pair.Key);
    }
}
int32 ABridgeWorld::RenderedFaceCount() const {int32 Count=0;for(const auto& Pair:Cells) if(IsValid(Pair.Value)) Count+=Pair.Value->FaceCount();return Count;}
int32 ABridgeWorld::RenderSectionCount() const {int32 Count=0;for(const auto& Pair:Cells) if(IsValid(Pair.Value)) Count+=Pair.Value->SectionCount();return Count;}
bool ABridgeWorld::BreakBlock(const FIntVector& Block) {
    if(!Sealed) return false;
    const FIntVector CellKey=CellOf(Block);auto* Data=Stored.Find(CellKey); if(!Data) return false;
    const FBridgeBlock* Visual=FindVisual(Block); FString PartnerId; FIntVector Partner=Block;
    if(Visual) { const auto Properties=StateProperties(Visual->StateKey); const FString Half=Properties.FindRef(TEXT("half"));
        if(Half==TEXT("upper") || Half==TEXT("lower")) {Partner.Y+=Half==TEXT("lower") ? 1 : -1;PartnerId=Visual->BlockId;}}
    const int32 Count=Data->RemoveAll([&](const FBridgeBlock& Shape){return OwnerOf(Shape)==Block;});
    if(!Count) return false;
    Shapes-=Count;MarkEdited(Block);RebuildCell(CellKey);
    if(!PartnerId.IsEmpty()) { const auto* Other=FindVisual(Partner); if(Other && Other->BlockId==PartnerId) {
        auto* PartnerData=Stored.Find(CellOf(Partner)); const int32 Removed=PartnerData->RemoveAll([&](const FBridgeBlock& Shape){return OwnerOf(Shape)==Partner;});
        Shapes-=Removed;MarkEdited(Partner);RebuildCell(CellOf(Partner));UpdateConnections(Partner);
    }}
    UpdateConnections(Block);return true;
}
const FBridgeBlock* ABridgeWorld::FindVisual(const FIntVector& Block) const {
    const auto* Data=Stored.Find(CellOf(Block)); if(!Data) return nullptr;
    for(const auto& Shape:*Data) if(OwnerOf(Shape)==Block && (Shape.Role==1 || Shape.Role==0)) return &Shape;
    return nullptr;
}
bool ABridgeWorld::AppendState(const FIntVector& Block,const FString& BlockId,int32 Color,const FString& Key,TArray<FBridgeBlock>& Out) const {
    if(!SavedPalette || SavedPalette->BlockstateDefinitions.IsEmpty()) {
        FBridgeBlock Shape;Shape.Position=FVector(Block)+FVector(.5)-ImportOrigin;Shape.BlockId=BlockId;Shape.Color=Color;
        Shape.Collision=true;Shape.SourceBlock=Block;Shape.HasSourceBlock=true;Out.Add(Shape);return true;
    }
    TArray<FBox> Collision,Outline; if(!SavedPalette->GetStateBoxes(BlockId,Key,Collision,Outline)) return false;
    const FVector ModelOffset=SavedPalette->GetModelOffset(BlockId,Block);
    FBridgeBlock Visual;Visual.Position=FVector(Block)+FVector(.5)+ModelOffset-ImportOrigin;Visual.BlockId=BlockId;Visual.Color=Color;
    Visual.SourceBlock=Block;Visual.HasSourceBlock=true;Visual.StateKey=Key;Visual.Role=1;Out.Add(Visual);
    FBridgeLightingService::StateProperties(SavedPalette,BlockId,Key,Out.Last().Opacity,Out.Last().Emission);Out.Last().HasLight=true;
    for(uint8 ShapeRole:{uint8(2),uint8(3)}) {
    if(ShapeRole==3 && Collision==Outline) continue;
    for(const auto& Box:ShapeRole==2 ? Collision : Outline) {
        FBridgeBlock Shape=Visual;Shape.Role=ShapeRole;Shape.Collision=ShapeRole==2;Shape.Size=Box.GetSize();
        Shape.Position=FVector(Block)+Box.GetCenter()+ModelOffset-ImportOrigin;Out.Add(Shape);
    }
    }
    return true;
}
FString ABridgeWorld::PlaceBlock(const FIntVector& Block,const FString& RequestedId,int32 Color,double Yaw,const FVector& Normal,const FVector& HitPoint) {
    const FIntVector CellKey=CellOf(Block);
    if(!Sealed || !Inside(CellKey) || !Revisions.Contains(CellKey)) return TEXT("outside import");
    auto* Data=Stored.Find(CellKey);if(!Data) return TEXT("cell unavailable");
    FString BlockId=RequestedId; const FVector McNormal(-Normal.Y,Normal.Z,Normal.X);
    if(FMath::Abs(Normal.Z)<.5 && SavedPalette) {
        FString Wall;
        if(BlockId==TEXT("minecraft:torch")) Wall=TEXT("minecraft:wall_torch");
        if(BlockId==TEXT("minecraft:soul_torch")) Wall=TEXT("minecraft:soul_wall_torch");
        if(BlockId==TEXT("minecraft:redstone_torch")) Wall=TEXT("minecraft:redstone_wall_torch");
        if(!Wall.IsEmpty() && !SavedPalette->DefaultState(Wall).IsEmpty()) BlockId=Wall;
    }
    FString Key=SavedPalette ? SavedPalette->DefaultState(BlockId) : FString(); auto Props=StateProperties(Key);
    const FString Facing=FacingForYaw(Yaw);
    const bool Stairs=BlockId.EndsWith(TEXT("_stairs")),Door=BlockId.EndsWith(TEXT("_door")) && !BlockId.EndsWith(TEXT("_trapdoor"));
    const FBridgeBlock* Occupant=FindVisual(Block);bool Merge=false;
    if(Occupant) {
        const auto Existing=StateProperties(Occupant->StateKey);
        if(Occupant->BlockId==BlockId && BlockId.EndsWith(TEXT("_slab")) && Existing.FindRef(TEXT("type"))!=TEXT("double")) {
            Props=Existing;Props.Add(TEXT("type"),TEXT("double"));Merge=true;
        } else return TEXT("occupied");
    }
    if(!Merge) {
        if(Props.Contains(TEXT("waterlogged"))) Props.Add(TEXT("waterlogged"),TEXT("false"));
        const bool MultiFace=BlockId==TEXT("minecraft:vine") || BlockId==TEXT("minecraft:glow_lichen")
            || BlockId==TEXT("minecraft:sculk_vein") || BlockId==TEXT("minecraft:resin_clump");
        if(MultiFace) {
            const FString Attached=FMath::Abs(McNormal.Y)>.5 ? (McNormal.Y>0 ? TEXT("down") : TEXT("up"))
                : FMath::Abs(McNormal.X)>.5 ? (McNormal.X>0 ? TEXT("west") : TEXT("east")) : (McNormal.Z>0 ? TEXT("north") : TEXT("south"));
            if(!Props.Contains(Attached)) return TEXT("this plant cannot attach to this face");
            for(const TCHAR* Face:{TEXT("up"),TEXT("down"),TEXT("north"),TEXT("south"),TEXT("east"),TEXT("west")})
                if(Props.Contains(Face)) Props.Add(Face,FString(Face)==Attached ? TEXT("true") : TEXT("false"));
        }
        if(Props.Contains(TEXT("axis"))) Props.Add(TEXT("axis"),FMath::Abs(McNormal.Y)>.5 ? TEXT("y") : FMath::Abs(McNormal.X)>.5 ? TEXT("x") : TEXT("z"));
        if(Props.Contains(TEXT("facing"))) {
            FString Direction=(Stairs || Door || BlockId.EndsWith(TEXT("_fence_gate"))) ? Facing : Opposite(Facing);
            if(BlockId.EndsWith(TEXT("wall_torch")) || BlockId.EndsWith(TEXT("wall_banner")))
                Direction=FMath::Abs(McNormal.X)>.5 ? (McNormal.X>0 ? TEXT("east") : TEXT("west")) : (McNormal.Z>0 ? TEXT("south") : TEXT("north"));
            if(Props.Contains(TEXT("face"))) {
                Props.Add(TEXT("face"),Normal.Z>.5 ? TEXT("floor") : Normal.Z<-.5 ? TEXT("ceiling") : TEXT("wall"));
                if(FMath::Abs(Normal.Z)<.5) Direction=FMath::Abs(McNormal.X)>.5 ? (McNormal.X>0 ? TEXT("east") : TEXT("west")) : (McNormal.Z>0 ? TEXT("south") : TEXT("north"));
            }
            // Six-direction components attach directly to the clicked face.
            TArray<FBox> TestA,TestB; auto Candidate=Props;Candidate.Add(TEXT("facing"),TEXT("up"));
            if(SavedPalette && SavedPalette->GetStateBoxes(BlockId,StateKey(Candidate),TestA,TestB))
                Direction=McNormal.Y>.5 ? TEXT("up") : McNormal.Y<-.5 ? TEXT("down") : FMath::Abs(McNormal.X)>.5 ? (McNormal.X>0 ? TEXT("east") : TEXT("west")) : (McNormal.Z>0 ? TEXT("south") : TEXT("north"));
            Props.Add(TEXT("facing"),Direction);
        }
        if(Props.Contains(TEXT("half"))) {
            const bool Top=Normal.Z<-.5 || (FMath::Abs(Normal.Z)<.5 && HitPoint.Z>BlockCenter(Block).Z);
            const FString Half=Props.FindRef(TEXT("half"));
            Props.Add(TEXT("half"),(Half==TEXT("upper") || Half==TEXT("lower")) ? TEXT("lower") : Top ? TEXT("top") : TEXT("bottom"));
        }
        if(BlockId.EndsWith(TEXT("_slab"))) Props.Add(TEXT("type"),Normal.Z<-.5 || (FMath::Abs(Normal.Z)<.5 && HitPoint.Z>BlockCenter(Block).Z) ? TEXT("top") : TEXT("bottom"));
        if(Props.Contains(TEXT("shape")) && Stairs) Props.Add(TEXT("shape"),TEXT("straight"));
        if(Props.Contains(TEXT("open"))) Props.Add(TEXT("open"),TEXT("false"));
        if(Props.Contains(TEXT("powered"))) Props.Add(TEXT("powered"),TEXT("false"));
        if(Props.Contains(TEXT("rotation"))) Props.Add(TEXT("rotation"),FString::FromInt((FMath::RoundToInt((Yaw+180.0)*16.0/360.0)%16+16)%16));
        if(Props.Contains(TEXT("shape")) && BlockId.Contains(TEXT("rail"))) Props.Add(TEXT("shape"),Facing==TEXT("north")||Facing==TEXT("south") ? TEXT("north_south") : TEXT("east_west"));
    }
    Key=StateKey(Props); TArray<FBridgeBlock> NewShapes;
    if(!AppendState(Block,BlockId,Color,Key,NewShapes)) return TEXT("unsupported block state; export/import textures again");
    FIntVector Partner=Block; bool TwoBlocks=Props.FindRef(TEXT("half"))==TEXT("lower");
    if(TwoBlocks) {
        ++Partner.Y; const FIntVector PartnerCell=CellOf(Partner);
        if(!Inside(PartnerCell) || !Stored.Contains(PartnerCell) || FindVisual(Partner)) return TEXT("upper block unavailable or occupied");
        auto Upper=Props;Upper.Add(TEXT("half"),TEXT("upper"));
        if(!AppendState(Partner,BlockId,Color,StateKey(Upper),NewShapes)) return TEXT("upper state unavailable");
    }
    const int32 Removing=Merge ? Data->FilterByPredicate([&](const FBridgeBlock& Shape){return OwnerOf(Shape)==Block;}).Num() : 0;
    TMap<FIntVector,int32> AddedByCell;for(const auto& Shape:NewShapes) ++AddedByCell.FindOrAdd(CellOf(OwnerOf(Shape)));
    for(const auto& Pair:AddedByCell) if(Stored.FindChecked(Pair.Key).Num()-(Pair.Key==CellKey ? Removing : 0)+Pair.Value>8192) return TEXT("cell shape limit");
    if(Shapes-Removing+NewShapes.Num()>4194304) return TEXT("shape limit");
    TSet<int32> Colors;TSet<FString> Groups;
    for(const auto& Shape:*Data) {Colors.Add(Shape.Color);Groups.Add(FString::Printf(TEXT("%s#%d#%d"),*Shape.BlockId,Shape.Color,Shape.Collision));}
    Colors.Add(Color);Groups.Add(FString::Printf(TEXT("%s#%d#1"),*BlockId,Color));
    if(Colors.Num()>512 || Groups.Num()>2048) return TEXT("material limit");
    // Native collision boxes, with a small inset to allow touching the supporting face.
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgePlace),false);
    if(Merge) if(auto* Existing=Cells.Find(CellKey)) if(IsValid(*Existing)) Params.AddIgnoredActor(Existing->Get());
    for(const auto& Shape:NewShapes) if(Shape.Collision) {
        if(Merge) for(const auto& Other:*Data) if(Other.Collision && OwnerOf(Other)!=Block) {
            const FVector A=Shape.Position,B=Other.Position;const FVector Half=(Shape.Size+Other.Size)*.5-FVector(.002);
            const FVector Distance=(A-B).GetAbs();if(Distance.X<Half.X && Distance.Y<Half.Y && Distance.Z<Half.Z) return TEXT("blocked by neighboring geometry");
        }
        const FVector Extent=FVector(Shape.Size.Z,Shape.Size.X,Shape.Size.Y)*50.0-FVector(.1);
        if(GetWorld()->OverlapBlockingTestByChannel(BridgeProtocol::ToUnreal(Shape.Position,ImportAnchor),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Extent.ComponentMax(FVector(.001))),Params)) return TEXT("blocked by body or geometry");
    }
    if(Merge) { Data->RemoveAll([&](const FBridgeBlock& Shape){return OwnerOf(Shape)==Block;});Shapes-=Removing; }
    for(const auto& Shape:NewShapes) { Stored.FindOrAdd(CellOf(OwnerOf(Shape))).Add(Shape);++Shapes; }
    MarkEdited(Block);if(TwoBlocks) MarkEdited(Partner);
    RebuildCell(CellKey);if(TwoBlocks && CellOf(Partner)!=CellKey) RebuildCell(CellOf(Partner));
    UpdateConnections(Block);if(TwoBlocks) UpdateConnections(Partner);return TEXT("placed");
}
void ABridgeWorld::UpdateConnections(const FIntVector& Block) {
    if(!SavedPalette || SavedPalette->StateShapes.IsEmpty()) return;
    TArray<FIntVector> Candidates{Block};
    for(const FString& Direction:{FString(TEXT("north")),FString(TEXT("south")),FString(TEXT("east")),FString(TEXT("west"))}) Candidates.Add(Block+Offset(Direction));
    Candidates.Add(Block+FIntVector(0,1,0));Candidates.Add(Block-FIntVector(0,1,0));
    TSet<FIntVector> Rebuild;
    for(const auto& Position:Candidates) {
        const auto* Source=FindVisual(Position);if(!Source) continue;
        const FBridgeBlock Original=*Source;auto Props=StateProperties(Original.StateKey);
        const bool Fence=Original.BlockId.EndsWith(TEXT("_fence"));
        const bool Wall=Original.BlockId.EndsWith(TEXT("_wall"));
        const bool Pane=Original.BlockId.EndsWith(TEXT("_pane")) || Original.BlockId==TEXT("minecraft:iron_bars");
        if(Fence || Wall || Pane) {
            TArray<FString> Connected;
            const auto* Above=FindVisual(Position+FIntVector(0,1,0));
            const bool Covered=Above && SavedPalette->HasSolidFace(Above->BlockId,Above->StateKey,TEXT("down"));
            for(const FString& Direction:{FString(TEXT("north")),FString(TEXT("south")),FString(TEXT("east")),FString(TEXT("west"))}) {
                const auto* Neighbor=FindVisual(Position+Offset(Direction));bool Connect=false;
                if(Neighbor) {
                    const bool Solid=SavedPalette->HasSolidFace(Neighbor->BlockId,Neighbor->StateKey,Opposite(Direction)) && !SavedPalette->CannotConnect(Neighbor->BlockId,Neighbor->StateKey);
                    const auto NeighborProps=StateProperties(Neighbor->StateKey);
                    const FString GateFacing=NeighborProps.FindRef(TEXT("facing"));
                    const bool Gate=Neighbor->BlockId.EndsWith(TEXT("_fence_gate")) && ((GateFacing==TEXT("north")||GateFacing==TEXT("south"))!=(Direction==TEXT("north")||Direction==TEXT("south")));
                    const bool MatchingFence=Neighbor->BlockId.EndsWith(TEXT("_fence")) && (Original.BlockId==TEXT("minecraft:nether_brick_fence"))==(Neighbor->BlockId==TEXT("minecraft:nether_brick_fence"));
                    Connect=Solid || (Fence && (MatchingFence || Gate))
                        || (Wall && (Neighbor->BlockId.EndsWith(TEXT("_wall")) || Gate))
                        || (Pane && (Neighbor->BlockId.EndsWith(TEXT("_pane")) || Neighbor->BlockId==TEXT("minecraft:iron_bars")));
                }
                Props.Add(Direction,Wall ? (Connect ? Covered ? TEXT("tall") : TEXT("low") : TEXT("none")) : Connect ? TEXT("true") : TEXT("false"));
                if(Connect) Connected.Add(Direction);
            }
            if(Wall && Props.Contains(TEXT("up"))) {
                const bool Straight=Connected.Num()==2 && Opposite(Connected[0])==Connected[1];
                Props.Add(TEXT("up"),!Covered && Straight ? TEXT("false") : TEXT("true"));
            }
        }
        if(Original.BlockId.EndsWith(TEXT("_stairs"))) {
            const FString Facing=Props.FindRef(TEXT("facing")),Half=Props.FindRef(TEXT("half"));FString Shape=TEXT("straight");
            auto Stair=[&](const FIntVector& P,TMap<FString,FString>& Neighbor)->bool {
                const auto* Other=FindVisual(P);if(!Other || !Other->BlockId.EndsWith(TEXT("_stairs"))) return false;
                Neighbor=StateProperties(Other->StateKey);return Neighbor.FindRef(TEXT("half"))==Half;
            };
            TMap<FString,FString> Front,Back;
            if(Stair(Position+Offset(Facing),Front)) {
                const FString OtherFacing=Front.FindRef(TEXT("facing"));
                if(OtherFacing!=Facing && OtherFacing!=Opposite(Facing)) {
                    TMap<FString,FString> Side;const bool Blocked=Stair(Position+Offset(Opposite(OtherFacing)),Side) && Side.FindRef(TEXT("facing"))==Facing;
                    if(!Blocked) Shape=OtherFacing==LeftOf(Facing) ? TEXT("outer_left") : TEXT("outer_right");
                }
            }
            if(Shape==TEXT("straight") && Stair(Position-Offset(Facing),Back)) {
                const FString OtherFacing=Back.FindRef(TEXT("facing"));
                if(OtherFacing!=Facing && OtherFacing!=Opposite(Facing)) {
                    TMap<FString,FString> Side;const bool Blocked=Stair(Position+Offset(OtherFacing),Side) && Side.FindRef(TEXT("facing"))==Facing;
                    if(!Blocked) Shape=OtherFacing==LeftOf(Facing) ? TEXT("inner_left") : TEXT("inner_right");
                }
            }
            Props.Add(TEXT("shape"),Shape);
        }
        const FString NewKey=StateKey(Props);if(NewKey==Original.StateKey) continue;
        TArray<FBridgeBlock> NewShapes;if(!AppendState(Position,Original.BlockId,Original.Color,NewKey,NewShapes)) continue;
        auto* Data=Stored.Find(CellOf(Position));if(!Data) continue;
        const int32 Before=Data->FilterByPredicate([&](const FBridgeBlock& S){return OwnerOf(S)==Position;}).Num();
        if(Data->Num()-Before+NewShapes.Num()>8192 || Shapes-Before+NewShapes.Num()>4194304) continue;
        Data->RemoveAll([&](const FBridgeBlock& S){return OwnerOf(S)==Position;});Data->Append(NewShapes);Shapes+=NewShapes.Num()-Before;Rebuild.Add(CellOf(Position));
        MarkEdited(Position);
    }
    for(const auto& Cell:Rebuild) RebuildCell(Cell);
}
bool ABridgeWorld::UseBlock(const FIntVector& Block,bool TimedRelease) {
    if(!Sealed || !SavedPalette) return false;
    const auto* Visual=FindVisual(Block);if(!Visual) return false;const FBridgeBlock Original=*Visual;
    auto Props=StateProperties(Original.StateKey);FString Property;
    if(Props.Contains(TEXT("open")) && (Original.BlockId.EndsWith(TEXT("_door")) || Original.BlockId.EndsWith(TEXT("_trapdoor")) || Original.BlockId.EndsWith(TEXT("_fence_gate")))) {
        if(Original.BlockId==TEXT("minecraft:iron_door") || Original.BlockId==TEXT("minecraft:iron_trapdoor")) return false;
        Property=TEXT("open");
    } else if((Original.BlockId.EndsWith(TEXT("_button")) || Original.BlockId==TEXT("minecraft:lever")) && Props.Contains(TEXT("powered"))) Property=TEXT("powered");
    else return false;
    if(Original.BlockId.EndsWith(TEXT("_button")) && Props.FindRef(Property)==TEXT("true") && !TimedRelease) return true;
    Props.Add(Property,Props.FindRef(Property)==TEXT("true") ? TEXT("false") : TEXT("true"));
    TArray<FIntVector> Positions{Block};const FString Half=Props.FindRef(TEXT("half"));
    if(Original.BlockId.EndsWith(TEXT("_door")) && (Half==TEXT("lower") || Half==TEXT("upper"))) Positions.Add(Block+FIntVector(0,Half==TEXT("lower") ? 1 : -1,0));
    TMap<FIntVector,TArray<FBridgeBlock>> Changes;
    for(const auto& Position:Positions) {
        const auto* Other=FindVisual(Position);if(!Other || Other->BlockId!=Original.BlockId) return false;
        auto State=StateProperties(Other->StateKey);State.Add(Property,Props.FindRef(Property));TArray<FBridgeBlock> NewShapes;
        if(!AppendState(Position,Other->BlockId,Other->Color,StateKey(State),NewShapes)) return false;
        Changes.Add(Position,MoveTemp(NewShapes));
    }
    TSet<FIntVector> Rebuild;
    for(const auto& Pair:Changes) {
        auto* Data=Stored.Find(CellOf(Pair.Key));if(!Data) return false;
        const int32 Before=Data->FilterByPredicate([&](const FBridgeBlock& S){return OwnerOf(S)==Pair.Key;}).Num();
        if(Data->Num()-Before+Pair.Value.Num()>8192 || Shapes-Before+Pair.Value.Num()>4194304) return false;
    }
    for(const auto& Pair:Changes) {
        auto* Data=Stored.Find(CellOf(Pair.Key));const int32 Before=Data->RemoveAll([&](const FBridgeBlock& S){return OwnerOf(S)==Pair.Key;});
        Data->Append(Pair.Value);Shapes+=Pair.Value.Num()-Before;Rebuild.Add(CellOf(Pair.Key));
        MarkEdited(Pair.Key);
    }
    if(Original.BlockId.EndsWith(TEXT("_button"))) {
        if(Props.FindRef(TEXT("powered"))==TEXT("true")) ButtonRelease.Add(Block,FPlatformTime::Seconds()+(Original.BlockId==TEXT("minecraft:stone_button") || Original.BlockId==TEXT("minecraft:polished_blackstone_button") ? 1.0 : 1.5));
        else ButtonRelease.Remove(Block);
    }
    for(const auto& Cell:Rebuild) RebuildCell(Cell);UpdateConnections(Block);
    if(InteractionSound) InteractionSound(Property==TEXT("open") ? (Props.FindRef(Property)==TEXT("true") ? TEXT("open") : TEXT("close"))
        : (Props.FindRef(Property)==TEXT("true") ? TEXT("activate") : TEXT("deactivate")),Original.BlockId,BlockCenter(Block));
    return true;
}
