#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeBiomeTint.h"
#include "BridgeBlockPalette.h"
#include "BridgeWorld.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeBiomeTintTest,"UEBridge.Blocks.BiomeTintCellValidationAndPlacement",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeBiomeTintTest::RunTest(const FString& Parameters) {
    TArray<FIntVector> Tints;Tints.Init(FIntVector(0x91bd59,0x77ab2f,0xa68f65),512);
    Tints[511]=FIntVector(0x80b497,0x60a17b,0xad9774);
    auto Field=BridgeBiomeTint::Write(Tints);auto CellObject=MakeShared<FJsonObject>();CellObject->SetObjectField(TEXT("biomeTints"),Field);
    TArray<FIntVector> Decoded;
    TestTrue(TEXT("Complete palette-backed color field decodes"),BridgeBiomeTint::Read(CellObject,Decoded));
    TestTrue(TEXT("All source colors including empty target voxels survive save"),Decoded==Tints);
    TestEqual(TEXT("Colors deduplicate instead of storing per-face triples"),Field->GetArrayField(TEXT("palette")).Num(),2);
    Field->SetArrayField(TEXT("indices"),{MakeShared<FJsonValueNumber>(0)});
    TestFalse(TEXT("Incomplete field cannot silently use player-point tint"),BridgeBiomeTint::Read(CellObject,Decoded));
    TestTrue(TEXT("Old snapshots without optional field remain accepted"),BridgeBiomeTint::Read(MakeShared<FJsonObject>(),Decoded));

    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Bridge=World->SpawnActor<ABridgeWorld>();
    auto* Palette=NewObject<UBridgeBlockPalette>();
    Palette->StateShapes.Add(TEXT("minecraft:grass_block"),TEXT(R"({"renderTints":{"0":9551193},"tintSources":{"0":"grass"}})"));
    Palette->StateShapes.Add(TEXT("minecraft:pink_petals"),TEXT(R"({"renderTints":{"0":16777215,"1":9551193},"tintSources":{"0":"none","1":"grass"}})"));
    Palette->StateShapes.Add(TEXT("minecraft:oak_leaves"),TEXT(R"({"renderTints":{"0":7842607},"tintSources":{"0":"foliage"}})"));
    Palette->StateShapes.Add(TEXT("minecraft:leaf_litter"),TEXT(R"({"renderTints":{"0":10915685},"tintSources":{"0":"dry_foliage"}})"));
    if(TestNotNull(TEXT("World bridge"),Bridge)) {
        FBridgePacket Begin;Begin.Kind=EBridgeKind::WorldBegin;Begin.Sequence=1;Begin.ImportId=TEXT("tint-placement");
        Bridge->BeginImport(Begin,FVector::ZeroVector);
        FBridgePacket Scope;Scope.Kind=EBridgeKind::WorldScope;Scope.Sequence=2;Scope.Cell=FIntVector(-1,-1,-1);Scope.Radius=1;Scope.HalfHeight=1;
        TestTrue(TEXT("Negative-coordinate scope"),Bridge->Handle(Scope,FVector::ZeroVector,nullptr,Palette));
        for(int32 X=-2;X<=0;++X) for(int32 Y=-2;Y<=0;++Y) for(int32 Z=-2;Z<=0;++Z) {
            FBridgePacket Cell;Cell.Kind=EBridgeKind::WorldCell;Cell.Sequence=3;Cell.SnapshotSequence=3;Cell.Cell=FIntVector(X,Y,Z);
            Cell.SnapshotId=TEXT("empty-biome-field");Cell.TotalBatches=1;Cell.BiomeTints=Tints;
            TestTrue(TEXT("Air cell retains native blended colors"),Bridge->Handle(Cell,FVector::ZeroVector,nullptr,Palette));
        }
        FBridgePacket Commit;Commit.Kind=EBridgeKind::WorldCommit;Commit.ImportId=Begin.ImportId;Commit.ImportCells=27;
        TestTrue(TEXT("All empty cells seal"),Bridge->CommitImport(Commit));
        const FIntVector Target(-1,-1,-1); // Local (7,7,7), index 511 in cell (-1,-1,-1).
        TestEqual(TEXT("Grass reads target coordinate, not captured player palette"),Bridge->RenderTintAt(Target,TEXT("minecraft:grass_block")),FColor(0x80,0xb4,0x97));
        TestEqual(TEXT("Foliage uses its native provider"),Bridge->RenderTintAt(Target,TEXT("minecraft:oak_leaves")),FColor(0x60,0xa1,0x7b));
        TestEqual(TEXT("Dry foliage uses its native provider"),Bridge->RenderTintAt(Target,TEXT("minecraft:leaf_litter")),FColor(0xad,0x97,0x74));
        TestEqual(TEXT("Petal base remains untinted"),Bridge->RenderTintAt(Target,TEXT("minecraft:pink_petals"),0),FColor::White);
        TestEqual(TEXT("Petal second tint uses target grass provider"),Bridge->RenderTintAt(Target,TEXT("minecraft:pink_petals"),1),FColor(0x80,0xb4,0x97));
        TestEqual(TEXT("Place grass into an originally empty position"),Bridge->PlaceBlock(Target,TEXT("minecraft:grass_block"),0xffffff),FString(TEXT("placed")));
        FString Id;FColor Tint;TestTrue(TEXT("Placed block resolves"),Bridge->GetBlockInfo(Target,Id,Tint));
        TestEqual(TEXT("Placed grass stores target biome color"),Tint,FColor(0x80,0xb4,0x97));
        Bridge->GetNativeBiomeTintCell(FIntVector(-1,-1,-1),Decoded);
        TestTrue(TEXT("Immutable save snapshot retains all source biome colors"),Decoded==Tints);
        Bridge->Clear();Bridge->GetNativeBiomeTintCell(FIntVector(-1,-1,-1),Decoded);
        TestTrue(TEXT("Replacing a world clears previous biome colors"),Decoded.IsEmpty());
    }
    World->DestroyWorld(false);return true;
}
#endif
