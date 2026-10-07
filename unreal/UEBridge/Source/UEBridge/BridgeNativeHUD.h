#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "BridgeNativeUiPalette.h"
#include "BridgeNativeHUD.generated.h"

/** Pixel-scaled local HUD; no SceneCapture, video decode or Minecraft window required. */
UCLASS()
class UEBRIDGE_API ABridgeNativeHUD : public AHUD {
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool HandlePointer(FKey Button, FVector2D Position);
    bool HandleScroll(int32 Delta);
    bool HandleText(TCHAR Character);
    bool HandleKey(FKey Key);
    bool HasSearchFocus() const;
private:
    struct FSlotHit { FBox2D Bounds; int32 Slot = -1; FString CatalogueItem; };
    TArray<FSlotHit> SlotHits;
    TArray<FBridgeNativeUiItem> Catalogue;
    UPROPERTY() TObjectPtr<class UBridgeNativeUiPalette> Resources;
    uint64 InventoryRevision = 0;
    FString Search, LastSelectedItem, Tooltip;
    double SelectedAt = 0;
    float GuiScale = 1, GuiWidth = 0, GuiHeight = 0;
    bool CatalogueMode = true, WasInventoryOpen = false, DebugVisible = false;
    int32 CatalogueRow = 0;
    FVector2D Pointer = FVector2D::ZeroVector;
    FBox2D SearchBounds, SearchWidgetBounds, TabBounds, PanelBounds;
    TArray<FBox2D> PauseButtons;
    TSharedPtr<class SWidget> SearchOverlay;
    TSharedPtr<class SEditableTextBox> SearchField;
    TWeakObjectPtr<class UGameViewportClient> SearchViewport;
    class ABridgeNativePlayerController* NativeController() const;
    class UBridgeNativeInventory* Inventory() const;
    void UpdateGuiScale();
    void RebuildCatalogue();
    void DrawHotbar();
    void DrawInventory();
    void DrawPauseMenu();
    void DrawHealth();
    void DrawPlayerPreview(float X, float Y);
    void Sprite(const FString& Key, float X, float Y, float W, float H, FLinearColor Color = FLinearColor::White);
    void Solid(float X, float Y, float W, float H, FLinearColor Color);
    void Item(const FString& ItemId, int32 Count, float X, float Y);
    void Text(const FString& Value, float X, float Y, FLinearColor Color = FLinearColor::White, bool Shadow = true);
    float TextWidth(const FString& Value) const;
    const FBridgeNativeGlyph* Glyph(int32 Codepoint) const;
    void EnsureSearchWidget();
    void UpdateSearchWidget(bool Visible);
};
