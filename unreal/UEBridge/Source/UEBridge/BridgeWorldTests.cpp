#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeWorld.h"
#include "BridgeCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

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
#endif
