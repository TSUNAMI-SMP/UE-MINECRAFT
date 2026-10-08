#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeWorld.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeNativeRulesTest,"UEBridge.Native.Rules.AtomicMovementAndGateDirection",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeNativeRulesTest::RunTest(const FString&) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Bridge=World->SpawnActor<ABridgeWorld>();
    if(!TestNotNull(TEXT("Rules world"),Bridge)) {World->DestroyWorld(false);return false;}
    FBridgePacket Begin;Begin.Kind=EBridgeKind::WorldBegin;Begin.Sequence=1;Begin.ImportId=TEXT("native-rules");Bridge->BeginImport(Begin,FVector::ZeroVector);
    FBridgePacket Scope;Scope.Kind=EBridgeKind::WorldScope;Scope.Sequence=2;Scope.Radius=1;Scope.HalfHeight=1;Bridge->Handle(Scope,FVector::ZeroVector,nullptr);
    for(int32 X=-1;X<=1;++X) for(int32 Y=-1;Y<=1;++Y) for(int32 Z=-1;Z<=1;++Z) {
        FBridgePacket Cell;Cell.Kind=EBridgeKind::WorldCell;Cell.Sequence=3;Cell.SnapshotSequence=3;Cell.Cell=FIntVector(X,Y,Z);Cell.SnapshotId=TEXT("empty");Cell.TotalBatches=1;
        TestTrue(TEXT("Import empty cell"),Bridge->Handle(Cell,FVector::ZeroVector,nullptr));
    }
    FBridgePacket Commit;Commit.Kind=EBridgeKind::WorldCommit;Commit.ImportId=Begin.ImportId;Commit.ImportCells=27;
    TestTrue(TEXT("Seal imported scope"),Bridge->CommitImport(Commit));
    const FIntVector From(7,2,2),To(8,2,2);
    TestTrue(TEXT("Place pushable block"),Bridge->SetNativeBlockState(From,TEXT("minecraft:stone"),TEXT("")));
    TestTrue(TEXT("Move across cell boundary atomically"),Bridge->ApplyNativeMutations({{From,FString(),FString(),0xffffff},{To,TEXT("minecraft:stone"),TEXT(""),0xffffff}}));
    FString Id,State;
    TestFalse(TEXT("Source was cleared"),Bridge->GetBlockState(From,Id,State));
    TestTrue(TEXT("Destination contains block"),Bridge->GetBlockState(To,Id,State));
    const int32 Before=Bridge->ShapeCount();
    TestFalse(TEXT("Out-of-scope transaction rejected"),Bridge->ApplyNativeMutations({{To,FString(),FString(),0xffffff},{FIntVector(99,2,2),TEXT("minecraft:stone"),TEXT(""),0xffffff}}));
    TestEqual(TEXT("Rejected mutation retains quantity"),Bridge->ShapeCount(),Before);
    TestTrue(TEXT("Rejected mutation retains source"),Bridge->GetBlockState(To,Id,State));
    TestFalse(TEXT("Duplicate owners rejected"),Bridge->ApplyNativeMutations({{To,FString(),FString(),0xffffff},{To,TEXT("minecraft:dirt"),TEXT(""),0xffffff}}));
    const FIntVector Gate(2,2,2);
    TestTrue(TEXT("Create powered east-facing repeater"),Bridge->SetNativeBlockState(Gate,TEXT("minecraft:repeater"),TEXT("delay=1,facing=east,locked=false,powered=true")));
    TestEqual(TEXT("Gate outputs opposite its input facing"),Bridge->NativeSignal(Gate,Gate-FIntVector(1,0,0)),15);
    TestEqual(TEXT("Gate never feeds its own input"),Bridge->NativeSignal(Gate,Gate+FIntVector(1,0,0)),0);
    TestEqual(TEXT("Gate never outputs sideways"),Bridge->NativeSignal(Gate,Gate+FIntVector(0,0,1)),0);
    TestTrue(TEXT("Create observer"),Bridge->SetNativeBlockState(Gate,TEXT("minecraft:observer"),TEXT("facing=east,powered=true")));
    TestEqual(TEXT("Observer outputs away from observed block"),Bridge->NativeSignal(Gate,Gate-FIntVector(1,0,0)),15);
    World->DestroyWorld(false);return true;
}
#endif
