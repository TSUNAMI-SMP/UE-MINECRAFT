#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "Dom/JsonObject.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeNativeFallingRestoreTest,"UEBridge.Native.Rules.LegacyFallingRestore",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeNativeFallingRestoreTest::RunTest(const FString&) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Bridge=World->SpawnActor<ABridgeWorld>();
    if(!TestNotNull(TEXT("Rules world"),Bridge)) {World->DestroyWorld(false);return false;}
    auto* Palette=NewObject<UBridgeBlockPalette>();
    Palette->Models.Add(TEXT("minecraft:block/restore_test"),TEXT(R"({"elements":[{"from":[0,0,0],"to":[16,16,16],"faces":{"up":{"texture":"minecraft:block/restore_test"}}}]})"));
    for(const TCHAR* Id:{TEXT("minecraft:gravel"),TEXT("minecraft:sand"),TEXT("minecraft:white_concrete")})
        Palette->BlockstateDefinitions.Add(Id,TEXT(R"({"variants":{"":{"model":"minecraft:block/restore_test"}}})"));
    Palette->FaceMaterials.Add(TEXT("minecraft:block/restore_test#0"),LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));
    FBridgePacket Begin;Begin.Kind=EBridgeKind::WorldBegin;Begin.Sequence=1;Begin.ImportId=TEXT("falling-restore");Bridge->BeginImport(Begin,FVector::ZeroVector);
    FBridgePacket Scope;Scope.Kind=EBridgeKind::WorldScope;Scope.Sequence=2;Scope.Radius=1;Scope.HalfHeight=1;Bridge->Handle(Scope,FVector::ZeroVector,nullptr,Palette);
    for(int X=-1;X<=1;++X) for(int Y=-1;Y<=1;++Y) for(int Z=-1;Z<=1;++Z) {
        FBridgePacket Cell;Cell.Kind=EBridgeKind::WorldCell;Cell.Sequence=3;Cell.SnapshotSequence=3;Cell.Cell=FIntVector(X,Y,Z);Cell.SnapshotId=TEXT("empty");Cell.TotalBatches=1;
        Bridge->Handle(Cell,FVector::ZeroVector,nullptr,Palette);
    }
    FBridgePacket Commit;Commit.Kind=EBridgeKind::WorldCommit;Commit.ImportId=Begin.ImportId;Commit.ImportCells=27;
    TestTrue(TEXT("Seal restore scope"),Bridge->CommitImport(Commit));
    auto Row=[](const TCHAR* Id,double Age,double Y) {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),Id);O->SetStringField(TEXT("state"),TEXT(""));O->SetNumberField(TEXT("tint"),7368816);O->SetNumberField(TEXT("age"),Age);
        O->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(.5),MakeShared<FJsonValueNumber>(Y),MakeShared<FJsonValueNumber>(.5)});
        O->SetArrayField(TEXT("velocity"),{MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(0)});
        return MakeShared<FJsonValueObject>(O);
    };
    AddExpectedError(TEXT("Bridge falling restore rejected"),EAutomationExpectedErrorFlags::Contains,1);
    TestFalse(TEXT("Invalid final row rejects all rows"),Bridge->ImportNativeFalling({Row(TEXT("minecraft:gravel"),611,1),Row(TEXT("minecraft:sand"),-1,1)}));
    TestEqual(TEXT("Rejected restore creates no entities"),Bridge->ExportNativeFalling().Num(),0);
    TestTrue(TEXT("Expired gravel, below-world sand, hardened concrete restore"),Bridge->ImportNativeFalling({Row(TEXT("minecraft:gravel"),611,1),Row(TEXT("minecraft:sand"),619,-49.833977277487392),Row(TEXT("minecraft:white_concrete"),604,2)}));
    const auto Saved=Bridge->ExportNativeFalling();
    if(TestEqual(TEXT("All three quantities retained"),Saved.Num(),3)) {
        for(const auto& Value:Saved) TestEqual(TEXT("Expired state is stable and saveable"),Value->AsObject()->GetNumberField(TEXT("age")),601.);
        const auto& P=Saved[1]->AsObject()->GetArrayField(TEXT("position"));
        TestEqual(TEXT("Recovered column keeps X"),P[0]->AsNumber(),.5);
        TestEqual(TEXT("Recovered column keeps Z"),P[2]->AsNumber(),.5);
        TestTrue(TEXT("Recovered Y is inside loaded scope"),P[1]->AsNumber()>=-8 && P[1]->AsNumber()<16);
    }
    World->DestroyWorld(false);return true;
}
#endif
