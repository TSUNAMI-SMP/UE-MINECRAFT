#include "BridgeNativeInventory.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryTransfers, "UEBridge.Native.Inventory.Transfers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryTransfers::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>();
    Contents->Initialize(nullptr, TEXT("automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Contents->AssignHotbar(TEXT("minecraft:stone"), 0, 63);
    TestTrue(TEXT("Right click picks the larger half"), Contents->ClickSlot(0, true));
    TestEqual(TEXT("Picked count"), Contents->GetCursor().Count, 32);
    TestEqual(TEXT("Source count"), Contents->GetSlots()[0].Count, 31);
    TestTrue(TEXT("Right click places one"), Contents->ClickSlot(9, true));
    TestEqual(TEXT("New stack count"), Contents->GetSlots()[9].Count, 1);
    TestEqual(TEXT("Cursor count"), Contents->GetCursor().Count, 31);
    TestTrue(TEXT("Return cursor"), Contents->ReturnCursor());
    TestEqual(TEXT("Merge source without exceeding stack size"), Contents->GetSlots()[0].Count, 62);
    TestTrue(TEXT("Shift moves to other inventory section"), Contents->ClickSlot(0, false, true));
    TestTrue(TEXT("Source empty after shift"), Contents->GetSlots()[0].IsEmpty());
    TestEqual(TEXT("Shift merged storage stack"), Contents->GetSlots()[9].Count, 63);
    TestTrue(TEXT("Number-key swap"), Contents->SwapSlots(9, 0));
    TestEqual(TEXT("Swapped hotbar count"), Contents->Selected().Count, 63);
    TestFalse(TEXT("Over-consumption rejected"), Contents->ConsumeSelected(64));
    TestEqual(TEXT("Failed consume preserves quantity"), Contents->Selected().Count, 63);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryFull, "UEBridge.Native.Inventory.FullInventoryPreservesItems", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryFull::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>();
    Contents->Initialize(nullptr, TEXT("automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    auto EmptySettings = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> EmptySlots;
    EmptySettings->SetArrayField(TEXT("inventory"), EmptySlots); Contents->ImportInitialSettings(EmptySettings);
    for (int32 I = 0; I < 36; ++I) { Contents->TakeCatalogue(TEXT("minecraft:stone")); Contents->ClickSlot(I, false); }
    TestFalse(TEXT("Full pickup rejected atomically"), Contents->AddStack(TEXT("minecraft:stone"), 1));
    TestEqual(TEXT("Partial pickup reports entire remainder"), Contents->InsertStack(TEXT("minecraft:stone"), 3), 3);
    Contents->TakeCatalogue(TEXT("minecraft:glass"));
    TestFalse(TEXT("Full carried stack cannot disappear on close"), Contents->ReturnCursor());
    TestEqual(TEXT("Cursor kept for next opening"), Contents->GetCursor().ItemId, FString(TEXT("minecraft:glass")));
    TestEqual(TEXT("Cursor quantity kept"), Contents->GetCursor().Count, 64);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryAmmunition, "UEBridge.Native.Inventory.AmmunitionIsAtomic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryAmmunition::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>();
    Contents->Initialize(nullptr, TEXT("automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Contents->AssignHotbar(TEXT("minecraft:arrow"), 0, 2);
    Contents->AssignHotbar(TEXT("minecraft:arrow"), 1, 3);
    Contents->TakeCatalogue(TEXT("minecraft:arrow"), true);
    TestEqual(TEXT("Cursor ammunition excluded from available count"), Contents->GetItemCount(TEXT("minecraft:arrow")), 5);
    TestFalse(TEXT("Insufficient ammunition leaves all stacks intact"), Contents->ConsumeItem(TEXT("minecraft:arrow"), 6));
    TestEqual(TEXT("Failed consume preserves first slot"), Contents->GetSlots()[0].Count, 2);
    TestEqual(TEXT("Failed consume preserves second slot"), Contents->GetSlots()[1].Count, 3);
    TestTrue(TEXT("Consumption can span hotbar stacks"), Contents->ConsumeItem(TEXT("minecraft:arrow"), 4));
    TestTrue(TEXT("Exhausted slot cleared"), Contents->GetSlots()[0].IsEmpty());
    TestEqual(TEXT("Second slot remainder preserved"), Contents->GetSlots()[1].Count, 1);
    TestEqual(TEXT("Cursor stack unchanged"), Contents->GetCursor().Count, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryRuntime, "UEBridge.Native.Inventory.RuntimeSnapshotRoundtrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryRuntime::RunTest(const FString&) {
    auto* Source = NewObject<UBridgeNativeInventory>();
    Source->Initialize(nullptr, TEXT("automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Source->AssignHotbar(TEXT("minecraft:stone"), 4, 12); Source->SelectHotbar(4);
    Source->TakeCatalogue(TEXT("minecraft:arrow"), true);
    const auto Saved = Source->ExportRuntimeState();
    auto* Target = NewObject<UBridgeNativeInventory>();
    Target->Initialize(nullptr, TEXT("automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestTrue(TEXT("Complete world inventory restores"), Target->ImportRuntimeState(Saved));
    TestEqual(TEXT("Selected slot restored"), Target->GetSelectedSlot(), 4);
    TestEqual(TEXT("Selected quantity restored"), Target->Selected().Count, 12);
    TestEqual(TEXT("Cursor item restored"), Target->GetCursor().ItemId, FString(TEXT("minecraft:arrow")));
    TestEqual(TEXT("Cursor quantity restored"), Target->GetCursor().Count, 1);
    auto Invalid = Target->ExportRuntimeState();
    auto Values = Invalid->GetArrayField(TEXT("slots"));
    auto BadStack = MakeShared<FJsonObject>(); BadStack->SetStringField(TEXT("item"), TEXT("minecraft:arrow")); BadStack->SetNumberField(TEXT("count"), 100);
    Values[35] = MakeShared<FJsonValueObject>(BadStack); Invalid->SetArrayField(TEXT("slots"), Values);
    Invalid->SetNumberField(TEXT("selected"), 0);
    const uint64 Revision = Target->GetRevision();
    TestFalse(TEXT("Bad final slot rejects entire snapshot"), Target->ImportRuntimeState(Invalid));
    TestEqual(TEXT("Failed restore does not change selected slot"), Target->GetSelectedSlot(), 4);
    TestEqual(TEXT("Failed restore does not change item count"), Target->Selected().Count, 12);
    TestEqual(TEXT("Failed restore does not change cursor"), Target->GetCursor().Count, 1);
    TestEqual(TEXT("Failed restore does not mutate revision"), Target->GetRevision(), Revision);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryOffhand, "UEBridge.Native.Inventory.OffhandTransfers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryOffhand::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>();
    Contents->Initialize(nullptr, TEXT("offhand-transfers"), false);
    auto EmptySettings = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> EmptySlots;
    EmptySettings->SetArrayField(TEXT("inventory"), EmptySlots); Contents->ImportInitialSettings(EmptySettings);
    Contents->AssignHotbar(TEXT("minecraft:stone"), 0, 12);
    TestTrue(TEXT("F moves main stack into empty offhand"), Contents->SwapOffhand());
    TestTrue(TEXT("Main hand becomes empty"), Contents->Selected().IsEmpty());
    TestEqual(TEXT("Offhand quantity is preserved"), Contents->GetOffhand().Count, 12);
    Contents->AssignHotbar(TEXT("minecraft:glass"), 0, 5);
    TestTrue(TEXT("F exchanges both stacks"), Contents->SwapOffhand());
    TestEqual(TEXT("Main item exchanged"), Contents->Selected().ItemId, FString(TEXT("minecraft:stone")));
    TestEqual(TEXT("Offhand item exchanged"), Contents->GetOffhand().ItemId, FString(TEXT("minecraft:glass")));
    TestTrue(TEXT("Right click takes larger offhand half"), Contents->ClickSlot(UBridgeNativeInventory::OffhandSlot, true));
    TestEqual(TEXT("Offhand split remainder"), Contents->GetOffhand().Count, 2);
    TestEqual(TEXT("Cursor split amount"), Contents->GetCursor().Count, 3);
    const uint64 Revision = Contents->GetRevision();
    TestFalse(TEXT("Carried cursor prevents destructive swap"), Contents->SwapOffhand());
    TestEqual(TEXT("Rejected swap preserves revision"), Contents->GetRevision(), Revision);
    TestTrue(TEXT("Cursor returns to main storage"), Contents->ReturnCursor());
    TestTrue(TEXT("Shift offhand moves to main inventory"), Contents->ClickSlot(UBridgeNativeInventory::OffhandSlot, false, true));
    TestTrue(TEXT("Quick move clears offhand"), Contents->GetOffhand().IsEmpty());
    TestEqual(TEXT("Full glass count retained"), Contents->GetItemCount(TEXT("minecraft:glass")), 5);
    TestEqual(TEXT("Full stone count retained"), Contents->GetItemCount(TEXT("minecraft:stone")), 12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryOffhandSnapshot, "UEBridge.Native.Inventory.OffhandSnapshotCompatibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryOffhandSnapshot::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>();
    Contents->Initialize(nullptr, TEXT("offhand-save"), false);
    Contents->AssignHotbar(TEXT("minecraft:torch"), 0, 17); Contents->SwapOffhand();
    Contents->AssignHotbar(TEXT("minecraft:stone"), 0, 3);
    const auto Saved = Contents->ExportRuntimeState();
    TestEqual(TEXT("Existing 36-slot representation remains stable"), Saved->GetArrayField(TEXT("slots")).Num(), 36);
    auto* Restored = NewObject<UBridgeNativeInventory>(); Restored->Initialize(nullptr, TEXT("offhand-restore"), false);
    TestTrue(TEXT("Offhand restores with atomic world inventory"), Restored->ImportRuntimeState(Saved));
    TestEqual(TEXT("Offhand saved item"), Restored->GetOffhand().ItemId, FString(TEXT("minecraft:torch")));
    TestEqual(TEXT("Offhand saved quantity"), Restored->GetOffhand().Count, 17);
    auto Malformed = Restored->ExportRuntimeState();
    auto BadStack = MakeShared<FJsonObject>(); BadStack->SetStringField(TEXT("item"), TEXT("minecraft:torch")); BadStack->SetNumberField(TEXT("count"), 100);
    Malformed->SetObjectField(TEXT("offhand"), BadStack);
    const uint64 Revision = Restored->GetRevision();
    TestFalse(TEXT("Malformed offhand rejects entire state"), Restored->ImportRuntimeState(Malformed));
    TestEqual(TEXT("Invalid state preserves offhand"), Restored->GetOffhand().Count, 17);
    TestEqual(TEXT("Invalid state preserves main hand"), Restored->Selected().Count, 3);
    TestEqual(TEXT("Invalid state preserves revision"), Restored->GetRevision(), Revision);
    Malformed->SetField(TEXT("offhand"), MakeShared<FJsonValueNull>());
    TestFalse(TEXT("Present null offhand cannot erase saved stack"), Restored->ImportRuntimeState(Malformed));
    Saved->RemoveField(TEXT("offhand"));
    TestTrue(TEXT("Pre-offhand native snapshots remain readable"), Restored->ImportRuntimeState(Saved));
    TestTrue(TEXT("Missing optional offhand is empty"), Restored->GetOffhand().IsEmpty());
    TestEqual(TEXT("Old selected stack preserved"), Restored->Selected().Count, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryOffhandArrows, "UEBridge.Native.Inventory.OffhandAmmunitionPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryOffhandArrows::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>(); Contents->Initialize(nullptr, TEXT("offhand-arrows"), false);
    Contents->AssignHotbar(TEXT("minecraft:arrow"), 0, 2); Contents->SwapOffhand();
    Contents->AssignHotbar(TEXT("minecraft:arrow"), 0, 3);
    TestFalse(TEXT("Shortage rejects combined offhand/main consumption"), Contents->ConsumeItem(TEXT("minecraft:arrow"), 6));
    TestEqual(TEXT("Failed request preserves offhand"), Contents->GetOffhand().Count, 2);
    TestTrue(TEXT("Bow takes an offhand arrow first"), Contents->ConsumeItem(TEXT("minecraft:arrow"), 1));
    TestEqual(TEXT("Offhand arrow consumed"), Contents->GetOffhand().Count, 1);
    TestEqual(TEXT("Hotbar arrows retained"), Contents->Selected().Count, 3);
    TestTrue(TEXT("Remaining request spans offhand/main atomically"), Contents->ConsumeItem(TEXT("minecraft:arrow"), 3));
    TestTrue(TEXT("Empty offhand cleared"), Contents->GetOffhand().IsEmpty());
    TestEqual(TEXT("Main arrow remainder"), Contents->Selected().Count, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryInitialOffhand, "UEBridge.Native.Inventory.InitialExportOffhand", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryInitialOffhand::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>(); Contents->Initialize(nullptr, TEXT("initial-offhand"), false);
    auto Settings = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> EmptySlots;
    Settings->SetArrayField(TEXT("inventory"), EmptySlots);
    auto Offhand = MakeShared<FJsonObject>(); Offhand->SetStringField(TEXT("id"), TEXT("minecraft:torch")); Offhand->SetNumberField(TEXT("count"), 11);
    Settings->SetObjectField(TEXT("offhand"), Offhand);
    Contents->ImportInitialSettings(Settings);
    TestEqual(TEXT("Existing Minecraft export offhand is imported"), Contents->GetOffhand().Count, 11);
    TestTrue(TEXT("An empty exported main inventory stays empty"), Contents->Selected().IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeInventoryProfileReuse, "UEBridge.Native.Inventory.ProfileReuseClearsPreviousStacks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeInventoryProfileReuse::RunTest(const FString&) {
    auto* Contents = NewObject<UBridgeNativeInventory>(); Contents->Initialize(nullptr, TEXT("reuse-first"), false);
    auto EmptySettings = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> EmptySlots;
    EmptySettings->SetArrayField(TEXT("inventory"), EmptySlots); Contents->ImportInitialSettings(EmptySettings);
    Contents->TakeCatalogue(TEXT("minecraft:diamond")); Contents->ClickSlot(20, false);
    Contents->AssignHotbar(TEXT("minecraft:torch"), 0, 5); Contents->SwapOffhand();
    Contents->TakeCatalogue(TEXT("minecraft:gold_ingot"), true); Contents->SelectHotbar(7);
    const uint64 Revision = Contents->GetRevision();
    Contents->Initialize(nullptr, TEXT("reuse-second"), false);
    TestEqual(TEXT("Profile change retains 36 main slots"), Contents->GetSlots().Num(), 36);
    TestTrue(TEXT("Previous storage stack is cleared"), Contents->GetSlots()[20].IsEmpty());
    TestTrue(TEXT("Previous carried cursor is cleared"), Contents->GetCursor().IsEmpty());
    TestTrue(TEXT("Previous offhand stack is cleared"), Contents->GetOffhand().IsEmpty());
    TestEqual(TEXT("Previous rare items are not duplicated into new profile"), Contents->GetItemCount(TEXT("minecraft:diamond")), 0);
    TestEqual(TEXT("Selection resets for new profile"), Contents->GetSelectedSlot(), 0);
    TestTrue(TEXT("Profile replacement advances inventory revision"), Contents->GetRevision() > Revision);
    Contents->AssignHotbar(TEXT("minecraft:diamond"), 7, 8);
    Contents->Initialize(nullptr, TEXT("reuse-second"), false);
    TestEqual(TEXT("Repeated initialization of same profile preserves live edits"), Contents->GetSlots()[7].Count, 8);
    return true;
}
#endif
