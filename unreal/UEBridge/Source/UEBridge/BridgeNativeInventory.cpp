#include "BridgeNativeInventory.h"
#include "BridgeNativeFile.h"
#include "BridgeNativeInventoryMath.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace {
const FBridgeNativeStack EmptyNativeStack;
}

bool UBridgeNativeInventory::ValidItemId(const FString& ItemId) {
    if (ItemId.Len() < 3 || ItemId.Len() > 256 || !ItemId.Contains(TEXT(":")) || ItemId.Contains(TEXT(".."))) return false;
    for (TCHAR C : ItemId) if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || C == ':' || C == '/' || C == '.' || C == '-')) return false;
    return true;
}

void UBridgeNativeInventory::Initialize(UBridgeNativeUiPalette* Resources, const FString& ProfileName,bool UseStandaloneProfile) {
    Palette = Resources;
    FString SafeName;
    for (TCHAR C : ProfileName.Left(64)) if (FChar::IsAlnum(C) || C == '_' || C == '-') SafeName.AppendChar(C);
    if (SafeName.IsEmpty()) SafeName = TEXT("default");
    if (Initialized && Profile == SafeName && StandaloneProfile == UseStandaloneProfile) return;
    // SetNum alone retains elements when a UObject is reused for another
    // profile. Establish an empty inventory before attempting any profile load.
    Profile = SafeName; Slots.Empty(36); Slots.SetNum(36); CursorStack.Clear(); OffhandStack.Clear(); ArmorStacks.Empty(4);ArmorStacks.SetNum(4); SelectedSlot = 0;
    Initialized = true; StandaloneProfile = UseStandaloneProfile; LastSaveAttempt = -1;
    if (!StandaloneProfile) PersistenceError.Reset();
    LoadedExistingProfile = StandaloneProfile && LoadProfile(); PreserveInvalidProfile = StandaloneProfile && !LoadedExistingProfile && FPaths::FileExists(ProfilePath()); InitialSettingsApplied = false;
    if (!LoadedExistingProfile) { SeedCreativeHotbar(); if (!PreserveInvalidProfile) Changed(); }
}

void UBridgeNativeInventory::ImportInitialSettings(const TSharedPtr<FJsonObject>& Settings) {
    if (LoadedExistingProfile || PreserveInvalidProfile || InitialSettingsApplied || !Settings.IsValid()) return;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Settings->TryGetArrayField(TEXT("inventory"), Values) && !Settings->TryGetArrayField(TEXT("hotbar"), Values)) return;
    TArray<FBridgeNativeStack> Initial; Initial.SetNum(36);
    for (const auto& Value : *Values) {
        const TSharedPtr<FJsonObject>* Entry = nullptr; FString ItemId; double Slot = -1, Count = 0;
        if (!Value.IsValid() || !Value->TryGetObject(Entry) || !Entry->IsValid() || !(*Entry)->TryGetNumberField(TEXT("slot"), Slot) || !(*Entry)->TryGetStringField(TEXT("id"), ItemId) || !(*Entry)->TryGetNumberField(TEXT("count"), Count)) return;
        if (!FMath::IsFinite(Slot) || !FMath::IsFinite(Count) || Slot < 0 || Slot >= 36 || FMath::FloorToDouble(Slot) != Slot || Count < 0 || Count > 99 || FMath::FloorToDouble(Count) != Count) return;
        if (Count == 0 || ItemId == TEXT("minecraft:air") || ItemId.IsEmpty()) continue;
        if (!ValidItemId(ItemId) || Count > MaxCount(ItemId)) return;
        Initial[int32(Slot)].ItemId = ItemId; Initial[int32(Slot)].Count = int32(Count);
    }
    FBridgeNativeStack InitialOffhand;
    if (const auto Value = Settings->TryGetField(TEXT("offhand")); Value.IsValid()) {
        const TSharedPtr<FJsonObject>* Entry = nullptr; FString ItemId; double Count = 0;
        if (!Value->TryGetObject(Entry) || !Entry->IsValid() || !(*Entry)->TryGetStringField(TEXT("id"), ItemId) || !(*Entry)->TryGetNumberField(TEXT("count"), Count)
            || !FMath::IsFinite(Count) || Count < 0 || Count > 99 || FMath::FloorToDouble(Count) != Count) return;
        if (Count > 0 && ItemId != TEXT("minecraft:air") && !ItemId.IsEmpty()) {
            if (!ValidItemId(ItemId) || Count > MaxCount(ItemId)) return;
            InitialOffhand.ItemId = ItemId; InitialOffhand.Count = int32(Count);
        }
    }
    TArray<FBridgeNativeStack> InitialArmor;InitialArmor.SetNum(4);
    const TArray<TSharedPtr<FJsonValue>>* ArmorValues=nullptr;
    if(Settings->HasField(TEXT("equipment")) && !Settings->TryGetArrayField(TEXT("equipment"),ArmorValues)) return;
    if(ArmorValues) {
        if(ArmorValues->Num()!=4) return;
        for(int32 I=0;I<4;++I) {
            const TSharedPtr<FJsonObject>* Entry=nullptr;FString Id;double Count=0;
            if(!(*ArmorValues)[I].IsValid() || !(*ArmorValues)[I]->TryGetObject(Entry) || !Entry->IsValid() || !(*Entry)->TryGetStringField(TEXT("id"),Id) || !(*Entry)->TryGetNumberField(TEXT("count"),Count) || !FMath::IsFinite(Count) || Count<0 || Count>1 || FMath::FloorToDouble(Count)!=Count) return;
            if(Count>0 && Id!=TEXT("minecraft:air") && !Id.IsEmpty()) {if(!ValidItemId(Id) || !CanInsertIntoSlot(ArmorBegin+I,Id)) return;InitialArmor[I].ItemId=Id;InitialArmor[I].Count=1;}
        }
    }
    Slots = MoveTemp(Initial); OffhandStack = MoveTemp(InitialOffhand);ArmorStacks=MoveTemp(InitialArmor); double Slot = 0;
    if (Settings->TryGetNumberField(TEXT("selectedSlot"), Slot) && FMath::IsFinite(Slot) && Slot >= 0 && Slot < 9 && FMath::FloorToDouble(Slot) == Slot) SelectedSlot = int32(Slot);
    InitialSettingsApplied = true; Changed();
}

void UBridgeNativeInventory::SeedCreativeHotbar() {
    const TCHAR* Starter[] = {TEXT("minecraft:grass_block"),TEXT("minecraft:stone"),TEXT("minecraft:oak_planks"),TEXT("minecraft:iron_block"),TEXT("minecraft:glass"),TEXT("minecraft:tnt"),TEXT("minecraft:torch"),TEXT("minecraft:water_bucket"),TEXT("minecraft:diamond_sword")};
    for (int32 I = 0; I < 9; ++I) {
        const FBridgeNativeUiItem* Entry = Palette ? Palette->FindItem(Starter[I]) : nullptr;
        if (!Palette || Entry) { Slots[I].ItemId = Starter[I]; Slots[I].Count = MaxCount(Starter[I]); }
    }
}

const FBridgeNativeStack& UBridgeNativeInventory::Selected() const { return Slots.IsValidIndex(SelectedSlot) ? Slots[SelectedSlot] : EmptyNativeStack; }
const FBridgeNativeStack& UBridgeNativeInventory::GetStack(int32 Slot) const {
    if(Slot>=ArmorBegin && Slot<ArmorEnd) return ArmorStacks.IsValidIndex(Slot-ArmorBegin) ? ArmorStacks[Slot-ArmorBegin] : EmptyNativeStack;
    return Slot == OffhandSlot ? OffhandStack : (Slots.IsValidIndex(Slot) ? Slots[Slot] : EmptyNativeStack);
}
FBridgeNativeStack* UBridgeNativeInventory::MutableStack(int32 Slot) {
    if (!Initialized) return nullptr;
    if(Slot>=ArmorBegin && Slot<ArmorEnd) return ArmorStacks.IsValidIndex(Slot-ArmorBegin) ? &ArmorStacks[Slot-ArmorBegin] : nullptr;
    return Slot == OffhandSlot ? &OffhandStack : (Slots.IsValidIndex(Slot) ? &Slots[Slot] : nullptr);
}

int32 UBridgeNativeInventory::MaxCount(const FString& ItemId) const {
    const auto* Entry = Palette ? Palette->FindItem(ItemId) : nullptr;
    return FMath::Clamp(Entry ? Entry->MaxCount : 64, 1, 99);
}

int32 UBridgeNativeInventory::EquipmentSlotFor(const FString& ItemId) const {
    if(const auto* Entry=Palette ? Palette->FindItem(ItemId) : nullptr) if(Entry->EquipmentSlot>0) return ArmorBegin+Entry->EquipmentSlot-1;
    // Backwards-compatible vanilla equipment identification for exports before component metadata.
    if(ItemId.EndsWith(TEXT("_helmet")) || ItemId==TEXT("minecraft:carved_pumpkin")) return ArmorBegin;
    if(ItemId.EndsWith(TEXT("_chestplate")) || ItemId==TEXT("minecraft:elytra")) return ArmorBegin+1;
    if(ItemId.EndsWith(TEXT("_leggings"))) return ArmorBegin+2;
    if(ItemId.EndsWith(TEXT("_boots"))) return ArmorBegin+3;
    return -1;
}
bool UBridgeNativeInventory::CanInsertIntoSlot(int32 Slot,const FString& ItemId) const {
    if(Slot>=ArmorBegin && Slot<ArmorEnd) return EquipmentSlotFor(ItemId)==Slot;
    return Slots.IsValidIndex(Slot) || Slot==OffhandSlot;
}
int32 UBridgeNativeInventory::SlotCapacity(int32 Slot,const FString& ItemId) const {return CanInsertIntoSlot(Slot,ItemId) ? (Slot>=ArmorBegin ? 1 : MaxCount(ItemId)) : 0;}
float UBridgeNativeInventory::GetArmorPoints() const {float Total=0;for(const auto& Stack:ArmorStacks) if(!Stack.IsEmpty()) if(const auto* Entry=Palette ? Palette->FindItem(Stack.ItemId) : nullptr) Total+=Entry->Armor;return Total;}
float UBridgeNativeInventory::GetArmorToughness() const {float Total=0;for(const auto& Stack:ArmorStacks) if(!Stack.IsEmpty()) if(const auto* Entry=Palette ? Palette->FindItem(Stack.ItemId) : nullptr) Total+=Entry->ArmorToughness;return Total;}
float UBridgeNativeInventory::GetArmorKnockbackResistance() const {float Total=0;for(const auto& Stack:ArmorStacks) if(!Stack.IsEmpty()) if(const auto* Entry=Palette ? Palette->FindItem(Stack.ItemId) : nullptr) Total+=Entry->ArmorKnockbackResistance;return FMath::Clamp(Total,0.f,1.f);}

void UBridgeNativeInventory::Changed() { Dirty = true; ++Revision; }

void UBridgeNativeInventory::SelectHotbar(int32 Slot) { if (Slot >= 0 && Slot < 9 && Slot != SelectedSlot) { SelectedSlot = Slot; Changed(); } }
void UBridgeNativeInventory::ScrollHotbar(int32 Delta) { SelectHotbar(((SelectedSlot - Delta) % 9 + 9) % 9); }

TArray<FBridgeNativeUiItem> UBridgeNativeInventory::FilterCatalogue(const FString& Search, const FBridgeNativeUiGroup* Group) const {
    TArray<FBridgeNativeUiItem> Result;
    if (!Palette) return Result;
    auto Add = [&](const FBridgeNativeUiItem& Entry) { if (Search.IsEmpty() || Entry.DisplayName.Contains(Search, ESearchCase::IgnoreCase) || Entry.ItemId.Contains(Search, ESearchCase::IgnoreCase)) Result.Add(Entry); };
    if (Group) { for (const FString& Id : Group->Items) if (const auto* Entry = Palette->FindItem(Id)) Add(*Entry); }
    else { for (const auto& Entry : Palette->Items) Add(Entry); }
    return Result;
}

bool UBridgeNativeInventory::AssignHotbar(const FString& ItemId, int32 Slot, int32 Count) {
    if (!Slots.IsValidIndex(Slot) || Slot >= 9 || !ValidItemId(ItemId) || Count <= 0 || (Palette && !Palette->FindItem(ItemId))) return false;
    Slots[Slot].ItemId = ItemId; Slots[Slot].Count = FMath::Min(Count, MaxCount(ItemId)); Changed(); return true;
}

int32 UBridgeNativeInventory::InsertRange(const FString& ItemId, int32 Count, int32 Begin, int32 End) {
    const int32 Limit = MaxCount(ItemId);
    for (int32 I = Begin; I < End && Count > 0; ++I) {
        if (Slots[I].ItemId == ItemId && Slots[I].Count < Limit) {
            const int32 Transfer = FMath::Min(Count, Limit - Slots[I].Count); Slots[I].Count += Transfer; Count -= Transfer;
        }
    }
    for (int32 I = Begin; I < End && Count > 0; ++I) if (Slots[I].IsEmpty()) {
        Slots[I].ItemId = ItemId; Slots[I].Count = FMath::Min(Count, Limit); Count -= Slots[I].Count;
    }
    return Count;
}

int32 UBridgeNativeInventory::InsertStack(const FString& ItemId, int32 Count) {
    if (!Initialized || !ValidItemId(ItemId) || Count <= 0 || Count > 4096) return Count;
    const int32 Remaining = InsertRange(ItemId, Count, 0, Slots.Num());
    if (Remaining != Count) Changed();
    return Remaining;
}

bool UBridgeNativeInventory::AddStack(const FString& ItemId, int32 Count) {
    if (Count <= 0 || Count > 4096 || !ValidItemId(ItemId)) return false;
    // This all-or-nothing entry point is appropriate for an action rollback.
    int32 Room = 0;
    for (const auto& Stack : Slots) if (Stack.IsEmpty()) Room += MaxCount(ItemId); else if (Stack.ItemId == ItemId) Room += MaxCount(ItemId) - Stack.Count;
    return Room >= Count && InsertStack(ItemId, Count) == 0;
}

bool UBridgeNativeInventory::QuickMove(int32 Slot) {
    auto* Target = MutableStack(Slot); if (!Target || Target->IsEmpty()) return false;
    auto& Stack = *Target;
    const int32 Equip=EquipmentSlotFor(Stack.ItemId);
    if(Slot<ArmorBegin && Equip>=ArmorBegin && GetStack(Equip).IsEmpty()) {
        if(auto* Armor=MutableStack(Equip)) {Armor->ItemId=Stack.ItemId;Armor->Count=1;if(--Stack.Count<=0) Stack.Clear();Changed();return true;}
    }
    const int32 Remaining = InsertRange(Stack.ItemId, Stack.Count, Slot < 9 ? 9 : 0, Slot < 9 || Slot >= OffhandSlot ? 36 : 9);
    if (Remaining == Stack.Count) return false;
    Stack.Count = Remaining; if (!Remaining) Stack.Clear(); Changed(); return true;
}

bool UBridgeNativeInventory::ClickSlot(int32 Slot, bool RightClick, bool Shift) {
    auto* Target = MutableStack(Slot); if (!Target) return false;
    if (Shift && CursorStack.IsEmpty()) return QuickMove(Slot);
    auto& Stack = *Target;
    if(!CursorStack.IsEmpty() && !CanInsertIntoSlot(Slot,CursorStack.ItemId)) return false;
    if (CursorStack.IsEmpty()) {
        if (Stack.IsEmpty()) return false;
        CursorStack.ItemId = Stack.ItemId; CursorStack.Count = RightClick ? (Stack.Count + 1) / 2 : Stack.Count;
        Stack.Count -= CursorStack.Count; if (Stack.Count <= 0) Stack.Clear();
    } else if (Stack.IsEmpty()) {
        Stack.ItemId = CursorStack.ItemId; Stack.Count = FMath::Min(RightClick ? 1 : CursorStack.Count,SlotCapacity(Slot,CursorStack.ItemId));
        CursorStack.Count -= Stack.Count; if (CursorStack.Count <= 0) CursorStack.Clear();
    } else if (Stack.ItemId == CursorStack.ItemId) {
        if (Stack.Count >= SlotCapacity(Slot,Stack.ItemId)) return false;
        const int32 Transfer = FMath::Min(RightClick ? 1 : CursorStack.Count, SlotCapacity(Slot,Stack.ItemId) - Stack.Count);
        Stack.Count += Transfer; CursorStack.Count -= Transfer; if (CursorStack.Count <= 0) CursorStack.Clear();
    } else {if(CursorStack.Count>SlotCapacity(Slot,CursorStack.ItemId)) return false;Swap(Stack, CursorStack); }
    Changed(); return true;
}

bool UBridgeNativeInventory::SwapSlots(int32 First, int32 Second) {
    auto* A = MutableStack(First); auto* B = MutableStack(Second);
    if (!A || !B || First == Second || !CursorStack.IsEmpty()) return false;
    if (A->IsEmpty() && B->IsEmpty()) return false;
    if(!A->IsEmpty() && (!CanInsertIntoSlot(Second,A->ItemId) || A->Count>SlotCapacity(Second,A->ItemId))) return false;
    if(!B->IsEmpty() && (!CanInsertIntoSlot(First,B->ItemId) || B->Count>SlotCapacity(First,B->ItemId))) return false;
    Swap(*A, *B); Changed(); return true;
}
bool UBridgeNativeInventory::SwapOffhand() { return SwapSlots(SelectedSlot, OffhandSlot); }

bool UBridgeNativeInventory::TakeCatalogue(const FString& ItemId, bool RightClick) {
    if (!ValidItemId(ItemId) || (Palette && !Palette->FindItem(ItemId))) return false;
    if (!CursorStack.IsEmpty() && CursorStack.ItemId != ItemId && !ReturnCursor()) return false;
    CursorStack.ItemId = ItemId; CursorStack.Count = RightClick ? 1 : MaxCount(ItemId); Changed(); return true;
}

bool UBridgeNativeInventory::ClickCatalogue(const FString& ItemId, bool RightClick, bool Shift) {
    if (!ItemId.IsEmpty() && (!ValidItemId(ItemId) || (Palette && !Palette->FindItem(ItemId)))) return false;
    if (!CursorStack.IsEmpty() && CursorStack.ItemId == ItemId) {
        if (RightClick) { if (--CursorStack.Count <= 0) CursorStack.Clear(); }
        else if (Shift) CursorStack.Count = MaxCount(ItemId);
        else if (CursorStack.Count < MaxCount(ItemId)) ++CursorStack.Count;
        else return false;
    } else if (CursorStack.IsEmpty() && !ItemId.IsEmpty()) {
        CursorStack.ItemId = ItemId; CursorStack.Count = Shift ? MaxCount(ItemId) : 1;
    } else if (!RightClick) {
        if (CursorStack.IsEmpty()) return false;
        CursorStack.Clear();
    } else if (!CursorStack.IsEmpty()) { if (--CursorStack.Count <= 0) CursorStack.Clear(); }
    else return false;
    Changed(); return true;
}

bool UBridgeNativeInventory::DeleteCreative(bool ClearInventory) {
    bool Mutated = false;
    if (ClearInventory) {
        for (auto& Stack : Slots) if (!Stack.IsEmpty()) { Stack.Clear(); Mutated = true; }
        if (!OffhandStack.IsEmpty()) { OffhandStack.Clear(); Mutated = true; }
        for(auto& Stack:ArmorStacks) if(!Stack.IsEmpty()) {Stack.Clear();Mutated=true;}
    } else if (!CursorStack.IsEmpty()) { CursorStack.Clear(); Mutated = true; }
    if (Mutated) Changed();
    return Mutated;
}

bool UBridgeNativeInventory::TakeSlot(int32 Slot, int32 Count, FBridgeNativeStack& Out) {
    auto* Stack=MutableStack(Slot); if(!Stack || Stack->IsEmpty() || Count<=0 || Stack->Count<Count) return false;
    Out.ItemId=Stack->ItemId;Out.Count=Count;Stack->Count-=Count;if(Stack->Count<=0) Stack->Clear();Changed();return true;
}

bool UBridgeNativeInventory::DistributeCursor(const TArray<int32>& TargetSlots,int32 Button,bool Creative) {
    if(CursorStack.IsEmpty() || Button<0 || Button>2 || (Button==2 && !Creative)) return false;
    TArray<int32> Eligible;std::vector<BridgeNativeInventoryMath::Slot> Counts;
    for(int32 Slot:TargetSlots) {
        if(Eligible.Contains(Slot) || (Button!=2 && CursorStack.Count<=Eligible.Num())) continue;
        const auto* Stack=MutableStack(Slot);if(!Stack || !CanInsertIntoSlot(Slot,CursorStack.ItemId) || (!Stack->IsEmpty() && Stack->ItemId!=CursorStack.ItemId)) continue;
        Eligible.Add(Slot);Counts.push_back({Stack->IsEmpty() ? 0 : Stack->Count,SlotCapacity(Slot,CursorStack.ItemId)});
    }
    if(Eligible.Num()==1) return Button<2 && ClickSlot(Eligible[0],Button==1);
    const auto Result=BridgeNativeInventoryMath::distribute(CursorStack.Count,Button,Counts);
    if(Result.counts.size()!=size_t(Eligible.Num()) || Eligible.IsEmpty()) return false;
    bool Mutated=false;
    for(int32 I=0;I<Eligible.Num();++I) if(auto* Stack=MutableStack(Eligible[I])) {
        const int32 Count=Result.counts[I];if(Stack->Count!=Count || Stack->ItemId!=CursorStack.ItemId) {Stack->ItemId=CursorStack.ItemId;Stack->Count=Count;Mutated=true;}
    }
    if(CursorStack.Count!=Result.cursor) {CursorStack.Count=Result.cursor;if(CursorStack.Count<=0) CursorStack.Clear();Mutated=true;}
    if(Mutated) Changed();return Mutated;
}

bool UBridgeNativeInventory::ReturnCursor() {
    if (CursorStack.IsEmpty()) return true;
    const int32 Remaining = InsertStack(CursorStack.ItemId, CursorStack.Count);
    if (Remaining != CursorStack.Count) { CursorStack.Count = Remaining; if (!Remaining) CursorStack.Clear(); Changed(); }
    return CursorStack.IsEmpty();
}

bool UBridgeNativeInventory::ConsumeSelected(int32 Count) {
    if (Count <= 0 || Selected().IsEmpty() || Selected().Count < Count) return false;
    Slots[SelectedSlot].Count -= Count; if (Slots[SelectedSlot].Count <= 0) Slots[SelectedSlot].Clear(); Changed(); return true;
}

int32 UBridgeNativeInventory::GetItemCount(const FString& ItemId) const {
    int32 Total = 0;
    if (!ValidItemId(ItemId)) return Total;
    for (const auto& Stack : Slots) if (Stack.ItemId == ItemId && Stack.Count > 0) Total += Stack.Count;
    if (OffhandStack.ItemId == ItemId && OffhandStack.Count > 0) Total += OffhandStack.Count;
    return Total;
}

bool UBridgeNativeInventory::ConsumeItem(const FString& ItemId,int32 Count) {
    if (!Initialized || Count <= 0 || Count > 4096 || GetItemCount(ItemId) < Count) return false;
    int32 Remaining = Count;
    if (OffhandStack.ItemId == ItemId && OffhandStack.Count > 0) {
        const int32 Taken = FMath::Min(OffhandStack.Count, Remaining);
        OffhandStack.Count -= Taken; Remaining -= Taken;
        if (OffhandStack.Count == 0) OffhandStack.Clear();
    }
    for (auto& Stack : Slots) {
        if (Remaining == 0) break;
        if (Stack.ItemId != ItemId || Stack.Count <= 0) continue;
        const int32 Taken = FMath::Min(Stack.Count, Remaining);
        Stack.Count -= Taken; Remaining -= Taken;
        if (Stack.Count == 0) Stack.Clear();
        if (Remaining == 0) break;
    }
    Changed(); return true;
}

bool UBridgeNativeInventory::TakeSelected(int32 Count, FBridgeNativeStack& Out) {
    Out.Clear(); if (Count <= 0 || Selected().IsEmpty() || Count > Selected().Count) return false;
    Out.ItemId = Selected().ItemId; Out.Count = Count; return ConsumeSelected(Count);
}

bool UBridgeNativeInventory::TakeCursor(int32 Count, FBridgeNativeStack& Out) {
    Out.Clear(); if (Count <= 0 || CursorStack.IsEmpty() || Count > CursorStack.Count) return false;
    Out.ItemId = CursorStack.ItemId; Out.Count = Count; CursorStack.Count -= Count; if (!CursorStack.Count) CursorStack.Clear(); Changed(); return true;
}

FString UBridgeNativeInventory::ProfilePath() const { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NativePlay"), TEXT("Profiles"), Profile + TEXT(".json")); }

bool UBridgeNativeInventory::LoadProfile() {
    FString Text;
    if (!FPaths::FileExists(ProfilePath())) { PersistenceError.Reset(); return false; }
    if (IFileManager::Get().FileSize(*ProfilePath()) > 128 * 1024 || !FFileHelper::LoadFileToString(Text, *ProfilePath())) { PersistenceError = TEXT("inventory_read_failed"); return false; }
    TSharedPtr<FJsonObject> Json;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) { PersistenceError = TEXT("inventory_invalid_json"); return false; }
    if (!ImportRuntimeState(Json)) return false;
    Dirty = false; return true;
}

bool UBridgeNativeInventory::ImportRuntimeState(const TSharedPtr<FJsonObject>& Json) {
    if (!Initialized || !Json.IsValid()) { PersistenceError = TEXT("inventory_invalid_runtime"); return false; }
    double Version = 0, SavedSelection = 0; const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Json->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !Json->TryGetArrayField(TEXT("slots"), Values) || Values->Num() != 36 || !Json->TryGetNumberField(TEXT("selected"), SavedSelection) || !FMath::IsFinite(SavedSelection) || SavedSelection < 0 || SavedSelection > 8 || FMath::FloorToDouble(SavedSelection) != SavedSelection) { PersistenceError = TEXT("inventory_invalid_profile"); return false; }
    auto ReadStack = [&](const TSharedPtr<FJsonValue>& Value, FBridgeNativeStack& Out) {
        const TSharedPtr<FJsonObject>* Entry = nullptr; FString ItemId; double Count = 0;
        if (!Value.IsValid() || !Value->TryGetObject(Entry) || !Entry->IsValid() || !(*Entry)->TryGetStringField(TEXT("item"), ItemId) || !(*Entry)->TryGetNumberField(TEXT("count"), Count) || !FMath::IsFinite(Count) || Count < 0 || Count > 99 || FMath::FloorToDouble(Count) != Count) return false;
        if (Count == 0 && ItemId.IsEmpty()) { Out.Clear(); return true; }
        if (!ValidItemId(ItemId) || Count < 1 || Count > MaxCount(ItemId)) return false;
        Out.ItemId = ItemId; Out.Count = int32(Count); return true;
    };
    TArray<FBridgeNativeStack> Loaded; Loaded.SetNum(36);
    for (int32 I = 0; I < 36; ++I) if (!ReadStack((*Values)[I], Loaded[I])) { PersistenceError = TEXT("inventory_invalid_stack"); return false; }
    FBridgeNativeStack LoadedCursor, LoadedOffhand;
    const auto CursorValue = Json->TryGetField(TEXT("cursor"));
    if (CursorValue.IsValid() && !ReadStack(CursorValue, LoadedCursor)) { PersistenceError = TEXT("inventory_invalid_cursor"); return false; }
    // Old native snapshots have exactly 36 slots and no offhand field. Keep
    // that representation valid, while rejecting a malformed present field.
    const auto OffhandValue = Json->TryGetField(TEXT("offhand"));
    if (OffhandValue.IsValid() && !ReadStack(OffhandValue, LoadedOffhand)) { PersistenceError = TEXT("inventory_invalid_offhand"); return false; }
    TArray<FBridgeNativeStack> LoadedArmor;LoadedArmor.SetNum(4);
    if(const auto ArmorValue=Json->TryGetField(TEXT("equipment"));ArmorValue.IsValid()) {
        const TArray<TSharedPtr<FJsonValue>>* ArmorValues=nullptr;
        if(!ArmorValue->TryGetArray(ArmorValues) || ArmorValues->Num()!=4) {PersistenceError=TEXT("inventory_invalid_equipment");return false;}
        for(int32 I=0;I<4;++I) if(!ReadStack((*ArmorValues)[I],LoadedArmor[I]) || (!LoadedArmor[I].IsEmpty() && (LoadedArmor[I].Count!=1 || !CanInsertIntoSlot(ArmorBegin+I,LoadedArmor[I].ItemId)))) {PersistenceError=TEXT("inventory_invalid_equipment");return false;}
    }
    Slots = MoveTemp(Loaded); CursorStack = MoveTemp(LoadedCursor); OffhandStack = MoveTemp(LoadedOffhand);ArmorStacks=MoveTemp(LoadedArmor); SelectedSlot = int32(SavedSelection);
    LoadedExistingProfile = true; InitialSettingsApplied = true; Changed(); PersistenceError.Reset(); return true;
}

TSharedPtr<FJsonObject> UBridgeNativeInventory::ExportRuntimeState() const {
    if (!Initialized) return nullptr;
    auto Json = MakeShared<FJsonObject>(); Json->SetNumberField(TEXT("version"), 1); Json->SetNumberField(TEXT("selected"), SelectedSlot);
    auto WriteStack = [](const FBridgeNativeStack& Stack) { auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("item"), Stack.IsEmpty() ? FString() : Stack.ItemId); Entry->SetNumberField(TEXT("count"), Stack.IsEmpty() ? 0 : Stack.Count); return MakeShared<FJsonValueObject>(Entry); };
    TArray<TSharedPtr<FJsonValue>> Values; for (const auto& Stack : Slots) Values.Add(WriteStack(Stack)); Json->SetArrayField(TEXT("slots"), Values); Json->SetField(TEXT("cursor"), WriteStack(CursorStack));
    Json->SetField(TEXT("offhand"), WriteStack(OffhandStack));
    TArray<TSharedPtr<FJsonValue>> Equipment;for(const auto& Stack:ArmorStacks) Equipment.Add(WriteStack(Stack));Json->SetArrayField(TEXT("equipment"),Equipment);
    return Json;
}

bool UBridgeNativeInventory::SaveProfile() {
    if (!Initialized) return false;
    // Native sessions save inventory together with terrain and dropped items in
    // one atomic world snapshot. A separate profile would replay item quantities
    // from a different instant after a crash.
    if (!StandaloneProfile) return true;
    if (PreserveInvalidProfile) return false;
    const auto Json = ExportRuntimeState();
    FString Text; if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text))) return false;
    const FString Path = ProfilePath(); FString Error;
    if (!BridgeNativeFile::WriteTextAtomic(Path, Text, Error)) { PersistenceError = TEXT("inventory_save_failed: ") + Error; UE_LOG(LogTemp, Warning, TEXT("Native inventory: cannot save profile %s: %s"), *Path, *Error); return false; }
    PersistenceError.Reset(); Dirty = false; return true;
}

void UBridgeNativeInventory::TickAutosave(double Now) { if (StandaloneProfile && Dirty && (LastSaveAttempt < 0 || Now - LastSaveAttempt >= 2)) { LastSaveAttempt = Now; SaveProfile(); } }
