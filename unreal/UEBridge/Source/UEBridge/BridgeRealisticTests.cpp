#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeRealisticWorld.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeRealisticPersistenceTest,"UEBridge.Realistic.SaveAndCleanupTransactions",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeRealisticPersistenceTest::RunTest(const FString&) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Source=World->SpawnActor<ABridgeRealisticWorld>();auto* Target=World->SpawnActor<ABridgeRealisticWorld>();
    if(!TestNotNull(TEXT("Source physics"),Source)||!TestNotNull(TEXT("Target physics"),Target)) {World->DestroyWorld(false);return false;}
    using namespace BridgeRealistic;
    Source->Physics.grains.push_back({{100,200,300},{1,2,3},true});
    Source->Physics.liquids.emplace(Key{0,0,0},Liquid{Water,1000});
    Source->Physics.liquids.emplace(Key{1,0,0},Liquid{Lava,500});
    Source->Physics.rocks.emplace(Key{2,0,0},700);Source->Physics.reacted=700;
    Source->Physics.bombs.push_back({{500,0,100},{0,0,5},2.5,1});Source->Physics.nextId=2;
    Source->Physics.gridOrigin={.3,17.25,12.4};
    Source->Physics.drops.push_back({{200,0,100},{2,0,-3},Water,100});Source->Physics.tick=42;
    if(!TestTrue(TEXT("Complete physical state restores"),Target->ImportState(Source->ExportState()))) {World->DestroyWorld(false);return false;}
    TestEqual(TEXT("Fractional grid origin restores"),Target->Physics.gridOrigin.y,17.25);
    TestEqual(TEXT("Water including droplets conserved"),Target->Physics.volume(Water),std::int64_t(1100));
    TestEqual(TEXT("Lava conserved"),Target->Physics.volume(Lava),std::int64_t(500));
    TestEqual(TEXT("Fuse and velocity restored"),Target->Physics.bombs[0].fuse,2.5);
    TestEqual(TEXT("Sleep state restored"),Target->Physics.grains[0].sleeping,true);
    TestEqual(TEXT("Solidification ledger restored"),Target->Physics.reacted,std::int64_t(700));
    auto Bad=Source->ExportState();auto Rows=Bad->GetArrayField(TEXT("liquids"));Rows.Last()->AsObject()->SetNumberField(TEXT("amount"),-1);
    TestFalse(TEXT("Invalid final row rejects entire snapshot"),Target->ImportState(Bad));
    TestEqual(TEXT("Failed restore keeps volume"),Target->Physics.volume(Water),std::int64_t(1100));
    TestEqual(TEXT("Failed restore keeps fuse"),Target->Physics.bombs[0].fuse,2.5);
    auto BadSources=Source->ExportState();TArray<TSharedPtr<FJsonValue>> TooManySources;
    for(int I=0;I<3;++I) {auto Row=MakeShared<FJsonObject>();Row->SetArrayField(TEXT("p"),TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(I*2000),MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(0)});TooManySources.Add(MakeShared<FJsonValueObject>(Row));}
    BadSources->SetArrayField(TEXT("niagaraWaterSources"),TooManySources);
    TestFalse(TEXT("Excess water sources reject the entire snapshot"),Target->ImportState(BadSources));
    TestEqual(TEXT("Rejected water snapshot preserves existing liquid"),Target->Physics.volume(Water),std::int64_t(1100));
    auto Legacy=Source->ExportState();Legacy->RemoveField(TEXT("gridOrigin"));Legacy->RemoveField(TEXT("niagaraWaterSources"));
    TestTrue(TEXT("Legacy zero-origin state rebases"),Target->ImportState(Legacy));
    TestEqual(TEXT("Legacy migration retains water including drops"),Target->Physics.volume(Water),std::int64_t(1100));
    TestEqual(TEXT("Legacy migration uses terrain grid"),Target->Physics.gridOrigin.y,17.25);
    TestTrue(TEXT("Legacy water cell has nearest terrain key"),Target->Physics.liquids.count(Key{0,-1,0})==1);
    Target->Command({TEXT("physics"),TEXT("clear"),TEXT("water"),TEXT("all")},FVector::ZeroVector);
    TestEqual(TEXT("Cleanup removes matching cells and droplets"),Target->Physics.volume(Water),std::int64_t(0));
    TestEqual(TEXT("Scoped type cleanup retains lava"),Target->Physics.volume(Lava),std::int64_t(500));
    Target->Command({TEXT("physics"),TEXT("undo")},FVector::ZeroVector);
    TestEqual(TEXT("Undo restores complete prior physical state"),Target->Physics.volume(Water),std::int64_t(1100));
    Target->Command({TEXT("physics"),TEXT("clear"),TEXT("all"),TEXT("all")},FVector::ZeroVector);
    TestTrue(TEXT("Global cleanup removes all persistent physics"),Target->Physics.grains.empty()&&Target->Physics.liquids.empty()&&Target->Physics.rocks.empty()&&Target->Physics.bombs.empty()&&Target->Physics.drops.empty());
    World->DestroyWorld(false);return true;
}
#endif
