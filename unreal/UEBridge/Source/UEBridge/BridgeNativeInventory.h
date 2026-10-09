#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BridgeNativeUiPalette.h"
#include "BridgeRecipeMath.h"
#include "BridgeNativeInventory.generated.h"

USTRUCT(BlueprintType)
struct FBridgeNativeStack {
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString ItemId;
    UPROPERTY(BlueprintReadOnly) int32 Count = 0;
    bool IsEmpty() const { return ItemId.IsEmpty() || Count <= 0; }
    void Clear() { ItemId.Reset(); Count = 0; }
};

/** UE-owned inventory. Slots 0..8 are the hotbar, 9..35 storage; slot 36 is offhand;37..40 are head/chest/legs/feet. */
UCLASS()
class UEBRIDGE_API UBridgeNativeInventory : public UObject {
    GENERATED_BODY()
public:
    static constexpr int32 CraftBegin=100;
    static constexpr int32 CraftOutput=109;
    static constexpr int32 ContainerBegin=200;
    static constexpr int32 ContainerOutput=227;
    bool OpenStation(const FString& Kind,const FString& Key);
    void CloseStation();
    FString GetStation() const {return Station;}
    int32 GetCraftWidth() const {return Station==TEXT("crafting_table") ? 3 : 2;}
    FBridgeNativeStack CraftResult() const;
    bool TakeCraftResult(bool Shift=false);
    FBridgeNativeStack StationResult() const;
    bool TakeStationResult(bool Shift=false);
    TArray<FString> StonecuttingResults() const;
    void SelectStonecutting(int32 Index) {StonecuttingIndex=Index;}
    void TickStations(float DeltaSeconds);
    float CookingProgress() const;
    float FuelProgress() const;
    bool EquipSelected();
    int32 ContainerSignal(const FString& Key) const;
    TArray<FBridgeNativeStack> ContainerContents(const FString& Key) const;
    void RemoveContainer(const FString& Key);
    TFunction<void(const FString&,bool)> FurnaceLitChanged;
    static int32 ContainerSize(const FString& Kind) {return Kind==TEXT("chest") ? 27 : Kind==TEXT("hopper") ? 5 : Kind==TEXT("dropper") || Kind==TEXT("dispenser") ? 9 : 3;}
    bool EnsureContainer(const FString& Key,const FString& Kind);
    int32 InsertContainer(const FString& Key,const FString& Id,int32 Count,int32 Side);
    bool TransferContainer(const FString& From,const FString& To,int32 FromSide,int32 ToSide);
    bool EmitContainerItem(const FString& Key,const TFunction<bool(const FBridgeNativeStack&)>& Spawn);
    static constexpr int32 OffhandSlot = 36;
    static constexpr int32 ArmorBegin = 37;
    static constexpr int32 ArmorEnd = 41;
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
    TArray<FBridgeNativeUiItem> FilterCatalogue(const FString& Search, const FBridgeNativeUiGroup* Group = nullptr) const;
    int32 MaxCount(const FString& ItemId) const;
    int32 EquipmentSlotFor(const FString& ItemId) const;
    bool CanInsertIntoSlot(int32 Slot, const FString& ItemId) const;
    int32 SlotCapacity(int32 Slot, const FString& ItemId) const;
    float GetArmorPoints() const;
    float GetArmorToughness() const;
    float GetArmorKnockbackResistance() const;
    bool DistributeCursor(const TArray<int32>& TargetSlots, int32 Button, bool Creative);
    bool AssignHotbar(const FString& ItemId, int32 Slot, int32 Count = 64);
    bool ClickSlot(int32 Slot, bool RightClick, bool Shift = false);
    bool SwapSlots(int32 First, int32 Second);
    bool SwapOffhand();
    bool TakeCatalogue(const FString& ItemId, bool RightClick = false);
    /** CreativeInventoryScreen.onMouseClick: matching entries adjust cursor; other entries discard it. */
    bool ClickCatalogue(const FString& ItemId, bool RightClick, bool Shift = false);
    bool DeleteCreative(bool ClearInventory = false);
    bool ReturnCursor();
    bool ConsumeSelected(int32 Count);
    int32 GetItemCount(const FString& ItemId) const;
    /** Consume offhand ammunition first, then storage/hotbar, after verifying the full amount. */
    bool ConsumeItem(const FString& ItemId,int32 Count);
    bool TakeSelected(int32 Count, FBridgeNativeStack& Out);
    bool TakeSlot(int32 Slot, int32 Count, FBridgeNativeStack& Out);
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
    UPROPERTY() TArray<FBridgeNativeStack> ArmorStacks;
    int32 SelectedSlot = 0;
    FString Profile, PersistenceError;
    bool Initialized = false, Dirty = false, LoadedExistingProfile = false, PreserveInvalidProfile = false, InitialSettingsApplied = false;
    bool StandaloneProfile = true;
    double LastSaveAttempt = -1;
    uint64 Revision = 0;
    TArray<FBridgeNativeStack> CraftGrid;
    struct FStation {TArray<FBridgeNativeStack> Slots;int32 Burn=0,BurnTotal=0,Cook=0;FString Kind,RecipeId;};
    TMap<FString,FStation> Stations;
    FString Station,StationKey;
    std::vector<BridgeRecipeMath::Recipe> Recipes;
    TMap<FString,int32> Fuels;
    TMap<FString,FString> Remainders;
    int32 StonecuttingIndex=0;
    float StationClock=0;
    const BridgeRecipeMath::Recipe* MatchingCraft() const;
    const BridgeRecipeMath::Recipe* MatchingSingle(const FStation& Data) const;
    bool ContainerAccepts(const FStation& Data,int32 Slot,const FString& Id,int32 Side) const;
    void LoadGameplay();
    void Changed();
    void SeedCreativeHotbar();
    FString ProfilePath() const;
    int32 InsertRange(const FString& ItemId, int32 Count, int32 Begin, int32 End);
    bool QuickMove(int32 Slot);
    FBridgeNativeStack* MutableStack(int32 Slot);
    static bool ValidItemId(const FString& ItemId);
};
