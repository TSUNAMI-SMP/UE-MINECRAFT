#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BridgeNativeUiPalette.h"
#include "BridgeNativeInventory.generated.h"

USTRUCT(BlueprintType)
struct FBridgeNativeStack {
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString ItemId;
    UPROPERTY(BlueprintReadOnly) int32 Count = 0;
    bool IsEmpty() const { return ItemId.IsEmpty() || Count <= 0; }
    void Clear() { ItemId.Reset(); Count = 0; }
};

/** UE-owned inventory. Slots 0..8 are the hotbar, 9..35 storage; slot 36 is the offhand. */
UCLASS()
class UEBRIDGE_API UBridgeNativeInventory : public UObject {
    GENERATED_BODY()
public:
    static constexpr int32 OffhandSlot = 36;
    void Initialize(UBridgeNativeUiPalette* Resources, const FString& ProfileName = TEXT("default"),bool UseStandaloneProfile = true);
    bool LoadProfile();
    bool SaveProfile();
    TSharedPtr<class FJsonObject> ExportRuntimeState() const;
    bool ImportRuntimeState(const TSharedPtr<class FJsonObject>& State);
    void ImportInitialSettings(const TSharedPtr<class FJsonObject>& Settings);
    void TickAutosave(double Now);
    void SelectHotbar(int32 Slot);
    void ScrollHotbar(int32 Delta);
    int32 GetSelectedSlot() const { return SelectedSlot; }
    FString GetSelectedItemId() const { return Selected().ItemId; }
    const FBridgeNativeStack& Selected() const;
    const TArray<FBridgeNativeStack>& GetSlots() const { return Slots; }
    const FBridgeNativeStack& GetCursor() const { return CursorStack; }
    const FBridgeNativeStack& GetOffhand() const { return OffhandStack; }
    const FBridgeNativeStack& GetStack(int32 Slot) const;
    uint64 GetRevision() const { return Revision; }
    UBridgeNativeUiPalette* GetPalette() const { return Palette; }
    TArray<FBridgeNativeUiItem> FilterCatalogue(const FString& Search) const;
    int32 MaxCount(const FString& ItemId) const;
    bool AssignHotbar(const FString& ItemId, int32 Slot, int32 Count = 64);
    bool ClickSlot(int32 Slot, bool RightClick, bool Shift = false);
    bool SwapSlots(int32 First, int32 Second);
    bool SwapOffhand();
    bool TakeCatalogue(const FString& ItemId, bool RightClick = false);
    bool ReturnCursor();
    bool ConsumeSelected(int32 Count);
    int32 GetItemCount(const FString& ItemId) const;
    /** Consume offhand ammunition first, then storage/hotbar, after verifying the full amount. */
    bool ConsumeItem(const FString& ItemId,int32 Count);
    bool TakeSelected(int32 Count, FBridgeNativeStack& Out);
    bool TakeCursor(int32 Count, FBridgeNativeStack& Out);
    bool AddStack(const FString& ItemId, int32 Count);
    /** Returns the uninserted count, so pickups never silently discard a remainder. */
    int32 InsertStack(const FString& ItemId, int32 Count);
    FString GetLastPersistenceError() const { return PersistenceError; }
private:
    UPROPERTY() TObjectPtr<UBridgeNativeUiPalette> Palette;
    UPROPERTY() TArray<FBridgeNativeStack> Slots;
    UPROPERTY() FBridgeNativeStack CursorStack;
    UPROPERTY() FBridgeNativeStack OffhandStack;
    int32 SelectedSlot = 0;
    FString Profile, PersistenceError;
    bool Initialized = false, Dirty = false, LoadedExistingProfile = false, PreserveInvalidProfile = false, InitialSettingsApplied = false;
    bool StandaloneProfile = true;
    double LastSaveAttempt = -1;
    uint64 Revision = 0;
    void Changed();
    void SeedCreativeHotbar();
    FString ProfilePath() const;
    int32 InsertRange(const FString& ItemId, int32 Count, int32 Begin, int32 End);
    bool QuickMove(int32 Slot);
    FBridgeNativeStack* MutableStack(int32 Slot);
    static bool ValidItemId(const FString& ItemId);
};
