// Requires UE; cloud validation exercises the Java escrow ledger without claiming
// that procedural rendering, UE physics or these engine tests have run.
#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeItemWorld.h"
#include "BridgeDroppedItem.h"
#include "BridgeBlockPalette.h"
#include "BridgeBlockPreview.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeItemTransactionsTest,"UEBridge.Items.NativeGroundAndApplicationReceipts",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeItemTransactionsTest::RunTest(const FString& Parameters) {
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);if(!TestNotNull(TEXT("World"),World)) return false;
    auto* Items=World->SpawnActor<ABridgeItemWorld>();auto* Player=World->SpawnActor<ACharacter>();
    if(!Items || !Player) {World->DestroyWorld(false);return false;}
    auto* Palette=NewObject<UBridgeBlockPalette>();
    Palette->ItemMaterials.Add(TEXT("local"),UMaterial::GetDefaultMaterial(MD_Surface));
    Palette->ItemModels.Add(TEXT("native"),TEXT("{\"ground\":[{\"texture\":\"local\",\"color\":16777215,\"vertices\":[[-0.1,0,-0.1],[0.1,0,-0.1],[0.1,0,0.1],[-0.1,0,0.1]],\"uv\":[[0,0],[1,0],[1,1],[0,1]]}]}"));
    Items->Palette=Palette;Items->SetAuthority(true,Player);
    FString Action;int32 Revision=-1,Requested=-1;
    Items->Result=[&](const FString& Tx,int32 Rev,const FString& Kind,int32 Count,const FString& Reason){Action=Kind;Revision=Rev;Requested=Count;};
    TestEqual(TEXT("Native ground model spawns"),Items->Drop(TEXT("tx"),TEXT("minecraft:diamond"),TEXT("native"),4,64,FVector(200,0,100),FVector::ZeroVector),FString(TEXT("item_spawned")));
    TestEqual(TEXT("Drop retry does not create another actor"),Items->Drop(TEXT("tx"),TEXT("minecraft:diamond"),TEXT("native"),4,64,FVector(200,0,100),FVector::ZeroVector),FString(TEXT("already_spawned")));
    TestEqual(TEXT("Exactly one actor"),Items->AliveCount(),1);Items->Tick(0);
    TestEqual(TEXT("Spawn application receipt"),Action,FString(TEXT("spawned")));TestEqual(TEXT("Spawn revision"),Revision,0);
    TestFalse(TEXT("Spawn receipt cannot credit inventory"),Items->Resolve(TEXT("tx"),0,4));TestTrue(TEXT("Spawn acknowledged"),Items->Resolve(TEXT("tx"),0,0));
    for(TActorIterator<ABridgeDroppedItem> It(World);It;++It) {for(int32 I=0;I<6;++I) It->Tick(.1f);It->SetActorLocation(Player->GetActorLocation());}
    Items->Tick(0);TestEqual(TEXT("Pickup requested"),Action,FString(TEXT("pickup")));TestEqual(TEXT("Escrow amount requested"),Requested,4);
    TestFalse(TEXT("Inventory cannot credit more than reserved"),Items->Resolve(TEXT("tx"),Revision,5));
    TestTrue(TEXT("Partial inventory capacity accepted"),Items->Resolve(TEXT("tx"),Revision,2));
    TestFalse(TEXT("Duplicate application receipt does not decrement again"),Items->Resolve(TEXT("tx"),Revision,2));
    for(TActorIterator<ABridgeDroppedItem> It(World);It;++It) TestEqual(TEXT("Exact uncredited amount remains visible"),It->GetQuantity(),2);
    Items->SetAuthority(false,Player);TestEqual(TEXT("Paused input preserves escrow actors"),Items->AliveCount(),1);
    Items->Clear();TestEqual(TEXT("Full session release clears actors"),Items->AliveCount(),0);
    Items->SetAuthority(true,Player);TestTrue(TEXT("Missing ground context rejected explicitly"),Items->Drop(TEXT("missing"),TEXT("minecraft:diamond"),TEXT("missing"),1,64,FVector(200,0,100),FVector::ZeroVector).StartsWith(TEXT("item_ground_model_missing")));
    World->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeItemNativeFloorTest,"UEBridge.Items.NativeCollisionFloorStopsWorldDynamicDrop",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeItemNativeFloorTest::RunTest(const FString& Parameters) {
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);if(!TestNotNull(TEXT("World"),World)) return false;
    auto* Floor=World->SpawnActor<ABridgeBlockPreview>();auto* Item=World->SpawnActor<ABridgeDroppedItem>(FVector(0,0,300),FRotator::ZeroRotator);
    if(!Floor || !Item) {World->DestroyWorld(false);return false;}
    FBridgeBlock Hull;Hull.Role=2;Hull.Collision=true;Hull.Size=FVector(10,1,10);Hull.Position=FVector(0,-.5,0);
    TArray<FBridgeBlock> Hulls;Hulls.Add(Hull);Floor->Replace(Hulls,FVector::ZeroVector,nullptr,nullptr,true);
    auto* Palette=NewObject<UBridgeBlockPalette>();Palette->ItemMaterials.Add(TEXT("local"),UMaterial::GetDefaultMaterial(MD_Surface));
    Palette->ItemModels.Add(TEXT("native"),TEXT("{\"ground\":[{\"texture\":\"local\",\"color\":16777215,\"vertices\":[[-0.1,0,-0.1],[0.1,0,-0.1],[0.1,0,0.1],[-0.1,0,0.1]],\"uv\":[[0,0],[1,0],[1,1],[0,1]]}]}"));
    TestTrue(TEXT("Ground geometry initialized"),Item->Initialize(Palette,TEXT("native"),1,FVector::ZeroVector));Item->SetActive(true);
    for(int32 Tick=0;Tick<60;++Tick) Item->Tick(.05f);
    TestTrue(TEXT("Dropped sphere remains above native collision floor"),Item->GetActorLocation().Z>=12.f);
    TestTrue(TEXT("Gravity brings the dropped sphere to the floor"),Item->GetActorLocation().Z<=16.f);
    World->DestroyWorld(false);return true;
}
#endif
