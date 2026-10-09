#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeWorld.h"
#include "BridgeCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "BridgeBlockPreview.h"
#include "Components/InstancedStaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeWorldSealTest,"UEBridge.World.ImportCommitAndProtection",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeWorldSealTest::RunTest(const FString& Parameters) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Character=World->SpawnActor<ABridgeCharacter>();
    if(TestNotNull(TEXT("Character"),Character)) {
        Character->SetAuthorityEnabled(true);
        auto* Movement=Character->GetCharacterMovement();
        TestTrue(TEXT("Landing retains walking authority"),Movement->DefaultLandMovementMode==MOVE_Walking);
        TestEqual(TEXT("Jump launch speed"),Movement->JumpZVelocity,900.f);
        TestTrue(TEXT("Gravity is approximately 3200cm/s2"),FMath::IsNearlyEqual(FMath::Abs(Movement->GetGravityZ()),3200.f,1.f));
        Character->ApplyFlight(true,true);TestTrue(TEXT("Creative can fly"),Character->BridgeFlying && Movement->MovementMode==MOVE_Flying);
        Character->ApplyFlight(false,true);TestTrue(TEXT("Survival revokes creative flight"),!Character->BridgeFlying && Movement->MovementMode==MOVE_Falling);
        Character->SetAuthorityEnabled(false);
        TestTrue(TEXT("Controller off freezes movement"),Movement->MovementMode==MOVE_None);
        Character->SetAuthorityEnabled(true);
        TestTrue(TEXT("Resume restores walking on next landing"),Movement->DefaultLandMovementMode==MOVE_Walking);
    }
    auto* Bridge=World->SpawnActor<ABridgeWorld>();
    if(!TestNotNull(TEXT("World bridge"),Bridge)) { World->DestroyWorld(false); return false; }
    FBridgePacket Begin; Begin.Kind=EBridgeKind::WorldBegin;Begin.Sequence=1;Begin.ImportId=TEXT("first");
    Bridge->BeginImport(Begin,FVector::ZeroVector);
    FBridgePacket Scope;Scope.Kind=EBridgeKind::WorldScope;Scope.Sequence=2;Scope.Radius=1;Scope.HalfHeight=1;
    Bridge->Handle(Scope,FVector::ZeroVector,nullptr);
    FBridgePacket Commit;Commit.Kind=EBridgeKind::WorldCommit;Commit.ImportId=TEXT("first");Commit.ImportCells=27;
    TestFalse(TEXT("Incomplete imports cannot seal"),Bridge->CommitImport(Commit));
    for(int32 X=-1;X<=1;++X) for(int32 Y=-1;Y<=1;++Y) for(int32 Z=-1;Z<=1;++Z) {
        FBridgePacket Cell;Cell.Kind=EBridgeKind::WorldCell;Cell.Sequence=3;Cell.SnapshotSequence=3;
        Cell.Cell=FIntVector(X,Y,Z);Cell.SnapshotId=TEXT("empty");Cell.TotalBatches=1;
        TestTrue(TEXT("Empty cell commit"),Bridge->Handle(Cell,FVector::ZeroVector,nullptr));
    }
    TestEqual(TEXT("Empty cells count as received"),Bridge->ImportedCells(),27);
    TestTrue(TEXT("All cells seal"),Bridge->CommitImport(Commit));TestTrue(TEXT("Sealed"),Bridge->IsSealed());
    const FIntVector Position(2,2,2);
    TestEqual(TEXT("Place in imported empty cell"),Bridge->PlaceBlock(Position,TEXT("minecraft:stone"),0x777777),FString(TEXT("placed")));
    TestEqual(TEXT("Collision and visual shape stored"),Bridge->ShapeCount(),1);
    FString SurfaceId; FColor SurfaceTint;
    TestTrue(TEXT("Authoritative sound and dust resolve block metadata"),Bridge->GetBlockInfo(Position,SurfaceId,SurfaceTint));
    TestEqual(TEXT("Surface sound resolves actual block"),SurfaceId,FString(TEXT("minecraft:stone")));
    TestEqual(TEXT("Surface retains imported color"),SurfaceTint,FColor(0x77,0x77,0x77));
    TestEqual(TEXT("Occupied cell rejected"),Bridge->PlaceBlock(Position,TEXT("minecraft:dirt"),0x888888),FString(TEXT("occupied")));
    TestEqual(TEXT("Placement beyond import blocked"),Bridge->PlaceBlock(FIntVector(99,99,99),TEXT("minecraft:stone"),0),FString(TEXT("outside import")));
    TestTrue(TEXT("Break imported block"),Bridge->BreakBlock(Position));TestEqual(TEXT("Break removes collision and visual"),Bridge->ShapeCount(),0);
    TestFalse(TEXT("Broken block cannot emit a stale support effect"),Bridge->GetBlockInfo(Position,SurfaceId,SurfaceTint));
    TestFalse(TEXT("Repeated break cannot mutate twice"),Bridge->BreakBlock(Position));
    const FIntVector Negative(-1,0,-1),First(0,0,0),Second(1,0,0);
    TestEqual(TEXT("Negative voxel placement"),Bridge->PlaceBlock(Negative,TEXT("minecraft:gold_block"),0xffffff),FString(TEXT("placed")));
    TestEqual(TEXT("First indexed row"),Bridge->PlaceBlock(First,TEXT("minecraft:stone"),0xffffff),FString(TEXT("placed")));
    TestEqual(TEXT("Second indexed row"),Bridge->PlaceBlock(Second,TEXT("minecraft:dirt"),0xffffff),FString(TEXT("placed")));
    FString IndexedId,IndexedState;
    TestTrue(TEXT("Negative source cell resolves"),Bridge->GetBlockState(Negative,IndexedId,IndexedState));
    TestEqual(TEXT("Negative voxel identity"),IndexedId,FString(TEXT("minecraft:gold_block")));
    TestTrue(TEXT("Remove row preceding another visual"),Bridge->BreakBlock(First));
    TestTrue(TEXT("Remaining index survives row compaction"),Bridge->GetBlockState(Second,IndexedId,IndexedState));
    TestEqual(TEXT("Compacted row keeps its identity"),IndexedId,FString(TEXT("minecraft:dirt")));
    TestFalse(TEXT("Removed row cannot resolve a stale visual"),Bridge->GetBlockState(First,IndexedId,IndexedState));
    TestTrue(TEXT("Blast removes the nearby indexed block"),Bridge->RemoveBlocksInSphere(Bridge->BlockCenter(Second),20)>0);
    TestFalse(TEXT("Bulk removal cannot resolve a stale visual"),Bridge->GetBlockState(Second,IndexedId,IndexedState));
    TestTrue(TEXT("Bulk removal preserves the distant negative voxel"),Bridge->GetBlockState(Negative,IndexedId,IndexedState));
    // Ordinary changed terrain must not enter the redstone/support backlog.
    Bridge->EnableNativeRules(0);
    TestEqual(TEXT("Normal terrain has no native rule backlog"),Bridge->PendingNativeRules(),0);
    TestEqual(TEXT("Stone edit remains available with rules enabled"),Bridge->PlaceBlock(FIntVector(3,2,2),TEXT("minecraft:stone"),0x777777),FString(TEXT("placed")));
    TestEqual(TEXT("Ordinary neighbor edits do not queue redstone evaluations"),Bridge->PendingNativeRules(),0);
    TestEqual(TEXT("Supported sand placement"),Bridge->PlaceBlock(FIntVector(3,3,2),TEXT("minecraft:sand"),0xffffff),FString(TEXT("placed")));
    TestEqual(TEXT("Support-sensitive block queues once"),Bridge->PendingNativeRules(),1);
    Bridge->Tick(.05f);
    TestEqual(TEXT("Stable support update drains"),Bridge->PendingNativeRules(),0);
    TestTrue(TEXT("Supported sand remains in terrain"),Bridge->GetBlockState(FIntVector(3,3,2),IndexedId,IndexedState));
    TestEqual(TEXT("Supported sand identity retained"),IndexedId,FString(TEXT("minecraft:sand")));
    Begin.Sequence=100;Bridge->BeginImport(Begin,FVector::ZeroVector);
    TestTrue(TEXT("Same import retry keeps sealed data"),Bridge->IsSealed());
    FBridgePacket Clear;Clear.Kind=EBridgeKind::WorldClear;Clear.Sequence=101;
    Bridge->Handle(Clear,FVector::ZeroVector,nullptr);
    TestEqual(TEXT("Source refresh cannot clear imported data"),Bridge->ImportedCells(),27);
    Commit.ImportId=TEXT("wrong");TestFalse(TEXT("Wrong import cannot commit"),Bridge->CommitImport(Commit));
    Begin.ImportId=TEXT("second");Bridge->BeginImport(Begin,FVector::ZeroVector);
    TestFalse(TEXT("Explicit new import unlocks"),Bridge->IsSealed());TestEqual(TEXT("Explicit replacement clears"),Bridge->ImportedCells(),0);
    World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeNativeProxyTest,"UEBridge.World.NativeProxyGroupingRetainsOwnerAndBiomePalette",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeNativeProxyTest::RunTest(const FString& Parameters) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Preview=World->SpawnActor<ABridgeBlockPreview>();
    if(!TestNotNull(TEXT("Proxy actor"),Preview)) {World->DestroyWorld(false);return false;}
    TArray<FBridgeBlock> Blocks;
    for(int32 I=0;I<2;++I) {
        FBridgeBlock Block;Block.Position=FVector(I+.5,.5,.5);Block.SourceBlock=FIntVector(I,0,0);Block.HasSourceBlock=true;
        Block.Role=2;Block.Collision=true;Block.BlockId=I==0 ? TEXT("minecraft:stone") : TEXT("minecraft:oak_leaves");
        Block.Color=I==0 ? 0x777777 : 0x48b518;Block.StateKey=I==0 ? TEXT("") : TEXT("persistent=true");Blocks.Add(Block);
    }
    Preview->Replace(Blocks,FVector::ZeroVector,nullptr,nullptr,true);
    TArray<UInstancedStaticMeshComponent*> Components;Preview->GetComponents(Components);
    TestEqual(TEXT("Different IDs/colors share one hidden response group"),Components.Num(),1);
    if(Components.Num()==1) {
        FBridgeBlock Resolved;
        TestTrue(TEXT("Second instance resolves"),Preview->ResolveHit(Components[0],1,Resolved));
        TestEqual(TEXT("Grouping retains actual block ID"),Resolved.BlockId,FString(TEXT("minecraft:oak_leaves")));
        TestTrue(TEXT("Grouping retains source voxel"),Resolved.SourceBlock==FIntVector(1,0,0));
        TestEqual(TEXT("Grouping retains native state"),Resolved.StateKey,FString(TEXT("persistent=true")));
    }
    auto* Bridge=World->SpawnActor<ABridgeWorld>();
    if(TestNotNull(TEXT("World bridge"),Bridge)) {
        FBridgePacket Scope;Scope.Kind=EBridgeKind::WorldScope;Scope.Sequence=1;Scope.Radius=1;Scope.HalfHeight=1;
        Bridge->Handle(Scope,FVector::ZeroVector,nullptr);
        FBridgePacket Cell;Cell.Kind=EBridgeKind::WorldCell;Cell.Sequence=2;Cell.SnapshotSequence=2;Cell.SnapshotId=TEXT("biomes");Cell.TotalBatches=1;
        for(int32 I=0;I<512;++I) {
            FBridgeBlock Block;Block.Position=FVector(I&7,(I>>6)&7,(I>>3)&7)+FVector(.5);Block.Color=I;Block.BlockId=TEXT("minecraft:grass_block");Cell.Blocks.Add(Block);
        }
        TestTrue(TEXT("A complete cell with native blended biome colors imports"),Bridge->Handle(Cell,FVector::ZeroVector,nullptr));
        TestEqual(TEXT("All 512 owners retained"),Bridge->ShapeCount(),512);
    }
    World->DestroyWorld(false);return true;
}
#endif
