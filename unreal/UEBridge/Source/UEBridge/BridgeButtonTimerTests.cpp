#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeButtonTimerTest,"UEBridge.World.ButtonReleaseUsesGameTimeAndRestoresPoweredSnapshots",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeButtonTimerTest::RunTest(const FString& Parameters) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Bridge=World->SpawnActor<ABridgeWorld>();
    if(!TestNotNull(TEXT("World bridge"),Bridge)) {World->DestroyWorld(false);return false;}
    auto* Palette=NewObject<UBridgeBlockPalette>();
    Palette->Models.Add(TEXT("minecraft:block/button_test"),TEXT(R"({"elements":[]})"));
    for(const TCHAR* Id:{TEXT("minecraft:stone_button"),TEXT("minecraft:oak_button"),TEXT("minecraft:lever")}) {
        Palette->BlockstateDefinitions.Add(Id,TEXT(R"({"variants":{"powered=true":{"model":"minecraft:block/button_test"},"powered=false":{"model":"minecraft:block/button_test"}}})"));
        Palette->StateShapes.Add(Id,TEXT(R"({"defaultState":"powered=false","states":{"powered=true":{"collision":[],"outline":[]},"powered=false":{"collision":[],"outline":[]}}})"));
    }
    FBridgePacket Begin;Begin.Kind=EBridgeKind::WorldBegin;Begin.Sequence=1;Begin.ImportId=TEXT("powered-buttons");
    Bridge->BeginImport(Begin,FVector::ZeroVector);
    FBridgePacket Scope;Scope.Kind=EBridgeKind::WorldScope;Scope.Sequence=2;Scope.Radius=1;Scope.HalfHeight=1;
    Bridge->Handle(Scope,FVector::ZeroVector,nullptr,Palette);
    const FIntVector Stone(2,2,2),Wood(4,2,2),Lever(6,2,2);
    for(int32 X=-1;X<=1;++X) for(int32 Y=-1;Y<=1;++Y) for(int32 Z=-1;Z<=1;++Z) {
        FBridgePacket Cell;Cell.Kind=EBridgeKind::WorldCell;Cell.Sequence=3;Cell.SnapshotSequence=3;
        Cell.Cell=FIntVector(X,Y,Z);Cell.SnapshotId=TEXT("buttons");Cell.TotalBatches=1;
        if(Cell.Cell==FIntVector::ZeroValue) {
            int32 Index=0;
            for(const TCHAR* Id:{TEXT("minecraft:stone_button"),TEXT("minecraft:oak_button"),TEXT("minecraft:lever")}) {
                FBridgeBlock Block;Block.SourceBlock=FIntVector(2+2*Index++,2,2);Block.HasSourceBlock=true;
                Block.Position=FVector(Block.SourceBlock)+FVector(.5);Block.BlockId=Id;Block.StateKey=TEXT("powered=true");Cell.Blocks.Add(Block);
            }
        }
        TestTrue(TEXT("Source cell imports"),Bridge->Handle(Cell,FVector::ZeroVector,nullptr,Palette));
    }
    FBridgePacket Commit;Commit.Kind=EBridgeKind::WorldCommit;Commit.ImportId=Begin.ImportId;Commit.ImportCells=27;
    TestTrue(TEXT("Powered source snapshot seals"),Bridge->CommitImport(Commit));
    TestEqual(TEXT("Only buttons receive restored scheduled ticks"),Bridge->ButtonRelease.Num(),2);
    const double Start=World->GetTimeSeconds();
    TestEqual(TEXT("Stone releases after 20 ticks"),Bridge->ButtonRelease.FindChecked(Stone),Start+1.0);
    TestEqual(TEXT("Wood releases after 30 ticks"),Bridge->ButtonRelease.FindChecked(Wood),Start+1.5);
    auto Powered=[&](const FIntVector& Block) {FString Id,State;return Bridge->GetBlockState(Block,Id,State) && State==TEXT("powered=true");};
    // Real time passing during a menu/save pause must not advance game time.
    for(int32 I=0;I<100;++I) Bridge->TickButtonTimers(Start+.5);
    TestTrue(TEXT("Paused stone remains powered"),Powered(Stone));
    TestTrue(TEXT("Paused wood remains powered"),Powered(Wood));
    Bridge->RefreshButtonTimersForCell(FIntVector::ZeroValue);
    TestEqual(TEXT("Neighbor refresh never extends an active button"),Bridge->ButtonRelease.FindChecked(Stone),Start+1.0);
    Bridge->TickButtonTimers(Start+1.0);
    TestFalse(TEXT("Stone releases at 20 ticks"),Powered(Stone));
    TestTrue(TEXT("Wood remains powered until 30 ticks"),Powered(Wood));
    Bridge->TickButtonTimers(Start+1.5);
    TestFalse(TEXT("Wood releases at 30 ticks"),Powered(Wood));
    TestTrue(TEXT("Lever state has no automatic release"),Powered(Lever));
    TestTrue(TEXT("Released wood can be pressed again"),Bridge->UseBlock(Wood));
    TestEqual(TEXT("Press creates one release tick"),Bridge->ButtonRelease.Num(),1);
    TestTrue(TEXT("Breaking an active button succeeds"),Bridge->BreakBlock(Wood));
    TestEqual(TEXT("Broken button loses its scheduled tick"),Bridge->ButtonRelease.Num(),0);
    World->DestroyWorld(false);return true;
}
#endif
