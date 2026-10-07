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
#endif
