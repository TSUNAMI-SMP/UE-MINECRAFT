#include "BridgeNativeInventory.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeCraftingTransactions,"UEBridge.Native.Crafting.ConservationAndResume",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeCraftingTransactions::RunTest(const FString&) {
    auto* Palette=NewObject<UBridgeNativeUiPalette>();
    for(const TCHAR* Id:{TEXT("minecraft:oak_log"),TEXT("minecraft:oak_planks"),TEXT("minecraft:iron_ore"),TEXT("minecraft:iron_ingot"),TEXT("minecraft:coal")}) {FBridgeNativeUiItem Item;Item.ItemId=Id;Palette->Items.Add(Item);}
    Palette->GameplayData=TEXT(R"({"version":1,"recipes":[{"id":"minecraft:planks","type":"minecraft:crafting_shapeless","ingredients":[["minecraft:oak_log"]],"result":"minecraft:oak_planks","count":4},{"id":"minecraft:iron","type":"minecraft:smelting","ingredients":[["minecraft:iron_ore"]],"result":"minecraft:iron_ingot","count":1,"ticks":200}],"fuels":{"minecraft:coal":1600},"remainders":{}})");
    auto* Inventory=NewObject<UBridgeNativeInventory>();Inventory->Initialize(Palette,TEXT("craft-automation"),false);
    auto Empty=MakeShared<FJsonObject>();Empty->SetArrayField(TEXT("inventory"),TArray<TSharedPtr<FJsonValue>>());Inventory->ImportInitialSettings(Empty);
    Inventory->TakeCatalogue(TEXT("minecraft:oak_log"));Inventory->ClickSlot(UBridgeNativeInventory::CraftBegin,false);
    TestEqual(TEXT("Recipe result is virtual before taking"),Inventory->CraftResult().Count,4);
    TestEqual(TEXT("Preview consumes no inputs"),Inventory->GetStack(UBridgeNativeInventory::CraftBegin).Count,64);
    TestTrue(TEXT("Output transaction succeeds"),Inventory->TakeCraftResult());
    TestEqual(TEXT("One log consumed"),Inventory->GetStack(UBridgeNativeInventory::CraftBegin).Count,63);
    TestEqual(TEXT("Four planks on cursor"),Inventory->GetCursor().Count,4);Inventory->ReturnCursor();
    Inventory->CloseStation();Inventory->OpenStation(TEXT("furnace"),TEXT("1,2,3"));
    Inventory->TakeCatalogue(TEXT("minecraft:iron_ore"),true);Inventory->ClickSlot(UBridgeNativeInventory::ContainerBegin,false);
    Inventory->TakeCatalogue(TEXT("minecraft:coal"),true);Inventory->ClickSlot(UBridgeNativeInventory::ContainerBegin+1,false);
    for(int I=0;I<100;++I) Inventory->TickStations(.05f);
    TestTrue(TEXT("Half cooked input remains"),!Inventory->GetStack(UBridgeNativeInventory::ContainerBegin).IsEmpty());
    const auto Saved=Inventory->ExportRuntimeState();auto* Restored=NewObject<UBridgeNativeInventory>();Restored->Initialize(Palette,TEXT("craft-resume"),false);
    TestTrue(TEXT("Cooking state snapshot restores"),Restored->ImportRuntimeState(Saved));Restored->OpenStation(TEXT("furnace"),TEXT("1,2,3"));
    for(int I=0;I<100;++I) Restored->TickStations(.05f);
    TestEqual(TEXT("Exactly one ingot after 200 ticks"),Restored->StationResult().Count,1);
    TestTrue(TEXT("Smelted input consumed"),Restored->GetStack(UBridgeNativeInventory::ContainerBegin).IsEmpty());
    TestTrue(TEXT("Result can be taken"),Restored->TakeStationResult());
    TestEqual(TEXT("Result on cursor"),Restored->GetCursor().ItemId,FString(TEXT("minecraft:iron_ingot")));
    Restored->ReturnCursor();
    TestTrue(TEXT("Create hopper storage"),Restored->EnsureContainer(TEXT("hopper"),TEXT("hopper")));
    TestTrue(TEXT("Create destination storage"),Restored->EnsureContainer(TEXT("chest"),TEXT("chest")));
    TestEqual(TEXT("Put exactly two items into hopper"),Restored->InsertContainer(TEXT("hopper"),TEXT("minecraft:oak_planks"),2,1),0);
    TestTrue(TEXT("Transfer one item"),Restored->TransferContainer(TEXT("hopper"),TEXT("chest"),-1,1));
    TestEqual(TEXT("Transfer removes one from source"),Restored->ContainerContents(TEXT("hopper"))[0].Count,1);
    TestEqual(TEXT("Transfer adds one to target"),Restored->ContainerContents(TEXT("chest"))[0].Count,1);
    TestFalse(TEXT("Failed item entity spawn does not consume"),Restored->EmitContainerItem(TEXT("hopper"),[](const FBridgeNativeStack&){return false;}));
    TestEqual(TEXT("Source remains after failed spawn"),Restored->ContainerContents(TEXT("hopper"))[0].Count,1);
    TestEqual(TEXT("Furnace sides reject ore"),Restored->InsertContainer(TEXT("1,2,3"),TEXT("minecraft:iron_ore"),1,0),1);
    TestEqual(TEXT("Furnace top accepts ore"),Restored->InsertContainer(TEXT("1,2,3"),TEXT("minecraft:iron_ore"),1,1),0);
    TestFalse(TEXT("Opening wrong container kind preserves existing contents"),Restored->EnsureContainer(TEXT("hopper"),TEXT("chest")));
    TestEqual(TEXT("Rejected kind retains hopper item"),Restored->ContainerContents(TEXT("hopper"))[0].Count,1);
    TestEqual(TEXT("One partially filled slot gives signal one"),Restored->ContainerSignal(TEXT("chest")),1);
    return true;
}
#endif
