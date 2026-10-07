#include "BridgeNativeHUD.h"
#include "BridgeNativeInventory.h"
#include "BridgeNativePlayerController.h"
#include "BridgeReceiver.h"
#include "BridgePlayerAppearance.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Misc/ScopeExit.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"

namespace {
TArray<int32> Codepoints(const FString& Value) {
    TArray<int32> Result;
    for (int32 I = 0; I < Value.Len(); ++I) {
        int32 Codepoint = uint32(Value[I]);
        if (sizeof(TCHAR) == 2 && Codepoint >= 0xd800 && Codepoint <= 0xdbff && I + 1 < Value.Len()) {
            const int32 Low = uint32(Value[I + 1]);
            if (Low >= 0xdc00 && Low <= 0xdfff) { Codepoint = 0x10000 + ((Codepoint - 0xd800) << 10) + Low - 0xdc00; ++I; }
        }
        Result.Add(Codepoint);
    }
    return Result;
}
bool Within(const FBox2D& Box, const FVector2D& Position) { return Box.bIsValid && Box.IsInside(Position); }
}

ABridgeNativePlayerController* ABridgeNativeHUD::NativeController() const { return Cast<ABridgeNativePlayerController>(GetOwningPlayerController()); }
UBridgeNativeInventory* ABridgeNativeHUD::Inventory() const { const auto* Control = NativeController(); return Control ? Control->GetNativeInventory() : nullptr; }

void ABridgeNativeHUD::UpdateGuiScale() {
    const auto* Control = NativeController();
    const int32 Requested = Control ? Control->GetNativeGuiScale() : 0;
    int32 Scale = 1;
    while ((Requested == 0 || Scale < Requested) && Canvas->SizeX / (Scale + 1) >= 320 && Canvas->SizeY / (Scale + 1) >= 240) ++Scale;
    if (Control && Control->GetNativeForceUnicode() && Scale % 2 != 0) ++Scale;
    GuiScale = float(Scale); GuiWidth = FMath::CeilToFloat(Canvas->SizeX / GuiScale); GuiHeight = FMath::CeilToFloat(Canvas->SizeY / GuiScale);
}

void ABridgeNativeHUD::Solid(float X, float Y, float W, float H, FLinearColor Color) { DrawRect(Color, X * GuiScale, Y * GuiScale, W * GuiScale, H * GuiScale); }

void ABridgeNativeHUD::Sprite(const FString& Key, float X, float Y, float W, float H, FLinearColor Color) {
    UTexture2D* Texture = Resources ? Resources->FindSprite(Key) : nullptr;
    if (Texture) DrawTexture(Texture, X * GuiScale, Y * GuiScale, W * GuiScale, H * GuiScale, 0, 0, 1, 1, Color, BLEND_Translucent);
}

const FBridgeNativeGlyph* ABridgeNativeHUD::Glyph(int32 Codepoint) const {
    if (!Resources) return nullptr;
    return Resources->Glyphs.FindByPredicate([&](const FBridgeNativeGlyph& Entry) { return Entry.Codepoint == Codepoint; });
}

float ABridgeNativeHUD::TextWidth(const FString& Value) const {
    float Width = 0;
    for (int32 C : Codepoints(Value)) { const auto* Entry = Glyph(C); Width += Entry ? Entry->Advance : (C == ' ' ? 4 : 6); }
    return Width;
}

void ABridgeNativeHUD::Text(const FString& Value, float X, float Y, FLinearColor Color, bool Shadow) {
    if (Value.IsEmpty()) return;
    if (!Resources || !Resources->FontAtlas || Resources->Glyphs.IsEmpty()) {
        if (Shadow) DrawText(Value, FLinearColor(0, 0, 0, Color.A), (X + 1) * GuiScale, (Y + 1) * GuiScale, GEngine ? GEngine->GetSmallFont() : nullptr, GuiScale);
        DrawText(Value, Color, X * GuiScale, Y * GuiScale, GEngine ? GEngine->GetSmallFont() : nullptr, GuiScale); return;
    }
    if (Shadow) Text(Value, X + 1, Y + 1, FLinearColor(Color.R * .25f, Color.G * .25f, Color.B * .25f, Color.A), false);
    const float AtlasW = Resources->FontAtlas->GetSizeX(), AtlasH = Resources->FontAtlas->GetSizeY();
    for (int32 C : Codepoints(Value)) {
        const auto* Entry = Glyph(C);
        if (!Entry) Entry = Glyph('?');
        if (Entry && Entry->Width > 0 && Entry->Height > 0) {
            const float W = Entry->DrawWidth > 0 ? Entry->DrawWidth : Entry->Width;
            const float H = Entry->DrawHeight > 0 ? Entry->DrawHeight : Entry->Height;
            DrawTexture(Resources->FontAtlas, X * GuiScale, (Y + 7 - Entry->Ascent) * GuiScale, W * GuiScale, H * GuiScale, Entry->X / AtlasW, Entry->Y / AtlasH, Entry->Width / AtlasW, Entry->Height / AtlasH, Color, BLEND_Translucent);
        }
        X += Entry ? Entry->Advance : 6;
    }
}

void ABridgeNativeHUD::Item(const FString& ItemId, int32 Count, float X, float Y) {
    if (ItemId.IsEmpty() || Count <= 0) return;
    const auto* Entry = Resources ? Resources->FindItem(ItemId) : nullptr;
    if (Entry && Entry->Icon) DrawTexture(Entry->Icon, X * GuiScale, Y * GuiScale, 16 * GuiScale, 16 * GuiScale, 0, 0, 1, 1, FLinearColor::White, BLEND_Translucent);
    else { Solid(X + 2, Y + 2, 12, 12, FLinearColor(.65f, .15f, .65f)); Text(TEXT("?"), X + 5, Y + 4); }
    if (Count > 1) { const FString Value = FString::FromInt(Count); Text(Value, X + 17 - TextWidth(Value), Y + 9); }
}

void ABridgeNativeHUD::DrawHotbar() {
    auto* Contents = Inventory(); if (!Contents) return;
    const float X = FMath::FloorToFloat((GuiWidth - 182) / 2), Y = GuiHeight - 22;
    if (Resources && Resources->FindSprite(TEXT("hud/hotbar"))) Sprite(TEXT("hud/hotbar"), X, Y, 182, 22);
    else { Solid(X, Y, 182, 22, FLinearColor(.1f, .1f, .1f, .8f)); for (int32 I = 0; I < 9; ++I) Solid(X + 3 + I * 20, Y + 3, 16, 16, FLinearColor(.35f, .35f, .35f)); }
    const float SelectionX = X - 1 + Contents->GetSelectedSlot() * 20;
    if (Resources && Resources->FindSprite(TEXT("hud/hotbar_selection"))) Sprite(TEXT("hud/hotbar_selection"), SelectionX, Y - 1, 24, 23);
    else { Solid(SelectionX, Y - 1, 24, 1, FLinearColor::White); Solid(SelectionX, Y + 21, 24, 1, FLinearColor::White); Solid(SelectionX, Y, 1, 21, FLinearColor::White); Solid(SelectionX + 23, Y, 1, 21, FLinearColor::White); }
    const auto& Slots = Contents->GetSlots(); for (int32 I = 0; I < 9 && Slots.IsValidIndex(I); ++I) Item(Slots[I].ItemId, Slots[I].Count, X + 3 + I * 20, Y + 3);
    const FString Selected = Contents->GetSelectedItemId();
    if (Selected != LastSelectedItem) { LastSelectedItem = Selected; SelectedAt = FPlatformTime::Seconds(); }
    const float Age = float(FPlatformTime::Seconds() - SelectedAt);
    const auto* Entry = Resources ? Resources->FindItem(Selected) : nullptr;
    if (!Selected.IsEmpty() && Age < 3) {
        const FString Name = Entry ? Entry->DisplayName : Selected;
        const float Alpha = FMath::Clamp((3 - Age) * 2, 0.f, 1.f);
        Text(Name, (GuiWidth - TextWidth(Name)) / 2, GuiHeight - 59, FLinearColor(1, 1, 1, Alpha));
    }
}

void ABridgeNativeHUD::DrawHealth() {
    const auto* Control = NativeController(); const auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr;
    if (!Receiver || Receiver->NativeCreative) return;
    const int32 HalfHearts = FMath::Clamp(FMath::CeilToInt(Receiver->GetNativeHealth()), 0, 20);
    const float X = FMath::FloorToFloat((GuiWidth - 182) / 2), Y = GuiHeight - 39;
    for (int32 I = 0; I < 10; ++I) {
        Sprite(TEXT("hud/heart/container"), X + I * 8, Y, 9, 9);
        const int32 Heart = HalfHearts - I * 2;
        if (Heart > 0) Sprite(Heart == 1 ? TEXT("hud/heart/half") : TEXT("hud/heart/full"), X + I * 8, Y, 9, 9);
    }
}

void ABridgeNativeHUD::RebuildCatalogue() { auto* Contents = Inventory(); Catalogue = Contents ? Contents->FilterCatalogue(Search) : TArray<FBridgeNativeUiItem>(); CatalogueRow = 0; }

void ABridgeNativeHUD::DrawPlayerPreview(float X, float Y) {
    const auto* Control = NativeController(); const auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr;
    if (!Receiver || !Receiver->PlayerAppearance || !Receiver->PlayerAppearance->SkinMaterial) return;
    UTexture* Skin = nullptr;
    if (!Receiver->PlayerAppearance->SkinMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("SkinTexture")), Skin) || !Skin) return;
    auto Piece = [&](float SX, float SY, float SW, float SH, float DX, float DY, float DW, float DH) {
        DrawTexture(Skin, (X + DX) * GuiScale, (Y + DY) * GuiScale, DW * GuiScale, DH * GuiScale, SX / 64, SY / 64, SW / 64, SH / 64, FLinearColor::White, BLEND_Translucent);
    };
    Piece(8, 8, 8, 8, 8, 0, 16, 16); Piece(40, 8, 8, 8, 7.5f, -.5f, 17, 17);
    Piece(20, 20, 8, 12, 8, 16, 16, 24); Piece(20, 36, 8, 12, 8, 16, 16, 24);
    const float Arm = Receiver->PlayerAppearance->IsSlim ? 6 : 8;
    Piece(44, 20, Receiver->PlayerAppearance->IsSlim ? 3 : 4, 12, 8 - Arm, 16, Arm, 24);
    Piece(36, 52, Receiver->PlayerAppearance->IsSlim ? 3 : 4, 12, 24, 16, Arm, 24);
    Piece(4, 20, 4, 12, 8, 40, 8, 24); Piece(20, 52, 4, 12, 16, 40, 8, 24);
}

void ABridgeNativeHUD::DrawInventory() {
    auto* Contents = Inventory(); const auto* Control = NativeController(); const auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr; if (!Contents || !Receiver) return;
    Solid(0, 0, GuiWidth, GuiHeight, FLinearColor(0, 0, 0, .62f)); SlotHits.Reset(); Tooltip.Reset();
    const bool Creative = Receiver->NativeCreative && CatalogueMode;
    const float W = Creative ? 195 : 176, H = Creative ? 136 : 166;
    const float X = FMath::FloorToFloat((GuiWidth - W) / 2), Y = FMath::FloorToFloat((GuiHeight - H) / 2);
    PanelBounds = FBox2D(FVector2D(X, Y) * GuiScale, FVector2D(X + W, Y + H) * GuiScale);
    Solid(X, Y, W, H, FLinearColor(.77f, .77f, .77f));
    if (Creative) Sprite(TEXT("container/creative_inventory/tab_item_search"), X, Y, W, H);
    else { Sprite(TEXT("container/inventory"), X, Y, 176, 166); DrawPlayerPreview(X + 33, Y + 12); }
    if (Receiver->NativeCreative) {
        TabBounds = FBox2D(FVector2D(X, Y - 22) * GuiScale, FVector2D(X + 74, Y - 2) * GuiScale);
        Solid(X, Y - 22, 74, 20, FLinearColor(.65f, .65f, .65f)); Text(Creative ? TEXT("所持品") : TEXT("アイテム検索"), X + 5, Y - 16, FLinearColor(.1f, .1f, .1f), false);
    } else TabBounds = FBox2D(ForceInit);
    auto DrawSlot = [&](int32 Index, float SX, float SY, const FString& CatalogueId = FString()) {
        Solid(SX - 1, SY - 1, 18, 18, FLinearColor(.35f, .35f, .35f)); Solid(SX, SY, 16, 16, FLinearColor(.55f, .55f, .55f));
        FSlotHit Hit; Hit.Bounds = FBox2D(FVector2D(SX, SY) * GuiScale, FVector2D(SX + 16, SY + 16) * GuiScale); Hit.Slot = Index; Hit.CatalogueItem = CatalogueId; SlotHits.Add(Hit);
        FString Id; int32 Count = 0;
        if (!CatalogueId.IsEmpty()) { Id = CatalogueId; Count = 1; }
        else if (Contents->GetSlots().IsValidIndex(Index)) { Id = Contents->GetSlots()[Index].ItemId; Count = Contents->GetSlots()[Index].Count; }
        Item(Id, Count, SX, SY);
        if (Within(Hit.Bounds, Pointer)) { Solid(SX, SY, 16, 16, FLinearColor(1, 1, 1, .3f)); const auto* Entry = Resources ? Resources->FindItem(Id) : nullptr; Tooltip = Entry ? Entry->DisplayName : Id; }
    };
    if (Creative) {
        Text(TEXT("アイテム検索"), X + 8, Y + 6, FLinearColor(.15f, .15f, .15f), false);
        SearchBounds = FBox2D(FVector2D(X + 82, Y + 6) * GuiScale, FVector2D(X + 171, Y + 17) * GuiScale);
        for (int32 Row = 0; Row < 5; ++Row) for (int32 Column = 0; Column < 9; ++Column) {
            const int32 Index = (CatalogueRow + Row) * 9 + Column;
            DrawSlot(-1, X + 9 + Column * 18, Y + 18 + Row * 18, Catalogue.IsValidIndex(Index) ? Catalogue[Index].ItemId : FString());
        }
        const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Catalogue.Num(), 9) - 5);
        Solid(X + 175, Y + 18, 12, 90, FLinearColor(.35f, .35f, .35f)); Solid(X + 175, Y + 18 + 75.f * CatalogueRow / Rows, 12, 15, FLinearColor(.7f, .7f, .7f));
        for (int32 I = 0; I < 9; ++I) DrawSlot(I, X + 9 + I * 18, Y + 112);
    } else {
        SearchBounds = FBox2D(ForceInit); Text(TEXT("インベントリ"), X + 8, Y + 72, FLinearColor(.15f, .15f, .15f), false);
        for (int32 Row = 0; Row < 3; ++Row) for (int32 Column = 0; Column < 9; ++Column) DrawSlot(9 + Row * 9 + Column, X + 8 + Column * 18, Y + 84 + Row * 18);
        for (int32 I = 0; I < 9; ++I) DrawSlot(I, X + 8 + I * 18, Y + 142);
        Text(TEXT("クラフトは未対応です"), X + 90, Y + 12, FLinearColor(.3f, .3f, .3f), false);
    }
    if (!Tooltip.IsEmpty() && Contents->GetCursor().IsEmpty()) {
        const float TX = FMath::Min(Pointer.X / GuiScale + 12, GuiWidth - TextWidth(Tooltip) - 6), TY = Pointer.Y / GuiScale - 12;
        Solid(TX - 3, TY - 3, TextWidth(Tooltip) + 6, 14, FLinearColor(.06f, .01f, .12f, .96f)); Text(Tooltip, TX, TY);
    }
    if (!Contents->GetCursor().IsEmpty()) Item(Contents->GetCursor().ItemId, Contents->GetCursor().Count, Pointer.X / GuiScale - 8, Pointer.Y / GuiScale - 8);
    UpdateSearchWidget(Creative);
}

void ABridgeNativeHUD::DrawPauseMenu() {
    Solid(0, 0, GuiWidth, GuiHeight, FLinearColor(0, 0, 0, .65f)); PauseButtons.Reset();
    const FString Title = TEXT("ゲームメニュー"); Text(Title, (GuiWidth - TextWidth(Title)) / 2, GuiHeight / 2 - 72);
    const auto* Control = NativeController(); const auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr;
    const FString Labels[] = {TEXT("ゲームに戻る"), TEXT("ワールドを保存"), TEXT("開始地点に戻る"),
        Receiver && Receiver->NativeLighting ? TEXT("照明: ON") : TEXT("照明: OFF"), TEXT("終了")};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Labels); ++I) {
        const float X = GuiWidth / 2 - 100, Y = GuiHeight / 2 - 32 + I * 24;
        FBox2D Bounds(FVector2D(X, Y) * GuiScale, FVector2D(X + 200, Y + 20) * GuiScale); PauseButtons.Add(Bounds);
        const bool Hovered = Within(Bounds, Pointer); Solid(X, Y, 200, 20, Hovered ? FLinearColor(.5f, .5f, .65f) : FLinearColor(.3f, .3f, .3f));
        Sprite(Hovered ? TEXT("widget/button_highlighted") : TEXT("widget/button"), X, Y, 200, 20);
        Text(Labels[I], (GuiWidth - TextWidth(Labels[I])) / 2, Y + 6);
    }
}

void ABridgeNativeHUD::DrawHUD() {
    Super::DrawHUD(); if (!Canvas) return;
    const auto* Control = NativeController(); auto* Contents = Inventory(); auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr;
    if (!Control || !Contents || !Receiver || !Receiver->NativePlayActive) { UpdateSearchWidget(false); return; }
    if (Resources != Contents->GetPalette()) { Resources = Contents->GetPalette(); RebuildCatalogue(); }
    UpdateGuiScale(); float MX = 0, MY = 0; if (GetOwningPlayerController()->GetMousePosition(MX, MY)) Pointer = FVector2D(MX, MY);
    if (Control->IsInventoryOpen()) {
        if (!WasInventoryOpen) { RebuildCatalogue(); WasInventoryOpen = true; }
        DrawInventory();
    } else {
        WasInventoryOpen = false; SlotHits.Reset(); UpdateSearchWidget(false);
        if (Control->IsPausedMenuOpen()) DrawPauseMenu();
        else if (Control->NativeHudVisible) { Sprite(TEXT("hud/crosshair"), FMath::FloorToFloat(GuiWidth / 2) - 7, FMath::FloorToFloat(GuiHeight / 2) - 7, 15, 15); DrawHotbar(); DrawHealth(); }
    }
    if (!Receiver->IsNativeReady() || !Control->IsSavedInventoryValid()) {
        const FString Message = !Control->IsSavedInventoryValid() ? Control->GetInventoryRestoreError()
            : Receiver->NativeStatus.IsEmpty() ? TEXT("ワールドを読み込み中…") : Receiver->NativeStatus;
        const float StatusY = Control->IsPausedMenuOpen() ? GuiHeight - 26 : GuiHeight / 2 - 12;
        Solid(0, StatusY, GuiWidth, 24, FLinearColor(0, 0, 0, .75f)); Text(Message, FMath::Max(2.f,(GuiWidth - TextWidth(Message)) / 2), StatusY + 8);
    }
    if (DebugVisible) {
        const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0;
        Text(FString::Printf(TEXT("UE %.0f fps / Native / %s"), Dt > 0 ? 1 / Dt : 0, *Receiver->NativeStatus), 2, 2);
        Text(FString::Printf(TEXT("UI: %s / %d icons / GUI x%.0f"), Resources ? *Resources->ExportId : TEXT("missing"), Resources ? Resources->Items.Num() : 0, GuiScale), 2, 13);
        Text(TEXT("Action: ") + Receiver->GetNativeLastAction(), 2, 24);
        if (!Contents->GetLastPersistenceError().IsEmpty()) Text(Contents->GetLastPersistenceError(), 2, 35, FLinearColor(1, .3f, .3f));
    }
}

bool ABridgeNativeHUD::HandlePointer(FKey Button, FVector2D Position) {
    auto* Control = NativeController(); auto* Contents = Inventory(); auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr;
    if (!Control || !Contents || !Receiver || (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton)) return false;
    Pointer = Position;
    if (Control->IsPausedMenuOpen()) {
        if (Button != EKeys::LeftMouseButton) return true;
        for (int32 I = 0; I < PauseButtons.Num(); ++I) if (Within(PauseButtons[I], Position)) {
            Receiver->NativeAction(TEXT("ui_click"));
            if (I == 0) Control->TogglePause();
            else if (I == 1) Receiver->NativeSave();
            else if (I == 2) { Receiver->NativeRespawn(); Control->TogglePause(); }
            else if (I == 3) Receiver->NativeSetLighting(!Receiver->NativeLighting);
            else if (I == 4) UKismetSystemLibrary::QuitGame(this, Control, EQuitPreference::Quit, false);
            return true;
        }
        return true;
    }
    if (!Control->IsInventoryOpen()) return false;
    if (Receiver->IsNativeSaving()) return true;
    if (Within(SearchBounds, Position)) { EnsureSearchWidget(); if (SearchField && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(SearchField, EFocusCause::Mouse); return true; }
    if (Within(TabBounds, Position)) { CatalogueMode = !CatalogueMode; return true; }
    const bool RightClick = Button == EKeys::RightMouseButton;
    const bool Shift = Control->IsInputKeyDown(EKeys::LeftShift) || Control->IsInputKeyDown(EKeys::RightShift);
    for (const auto& Hit : SlotHits) if (Within(Hit.Bounds, Position)) {
        if (!Hit.CatalogueItem.IsEmpty()) {
            if (Shift) Contents->AssignHotbar(Hit.CatalogueItem, Contents->GetSelectedSlot(), Contents->MaxCount(Hit.CatalogueItem));
            else Contents->TakeCatalogue(Hit.CatalogueItem, RightClick);
        } else if (Hit.Slot >= 0) Contents->ClickSlot(Hit.Slot, RightClick, Shift);
        return true;
    }
    if (!Within(PanelBounds, Position) && !Contents->GetCursor().IsEmpty()) {
        const FString ItemId = Contents->GetCursor().ItemId;
        const int32 Count = RightClick ? 1 : Contents->GetCursor().Count;
        if (Receiver->NativeDrop(ItemId, Count)) { FBridgeNativeStack Dropped; Contents->TakeCursor(Count, Dropped); }
    }
    return true;
}

bool ABridgeNativeHUD::HandleScroll(int32 Delta) {
    const auto* Control = NativeController(); if (!Control || !Control->IsInventoryOpen() || !CatalogueMode) return false;
    const int32 Maximum = FMath::Max(0, FMath::DivideAndRoundUp(Catalogue.Num(), 9) - 5); CatalogueRow = FMath::Clamp(CatalogueRow - Delta, 0, Maximum); return true;
}

bool ABridgeNativeHUD::HandleText(TCHAR Character) {
    const auto* Control = NativeController(); if (!Control || !Control->IsInventoryOpen() || !CatalogueMode || !Within(SearchBounds, Pointer) || HasSearchFocus()) return false;
    if (Character < ' ' || Character == 127 || Search.Len() >= 128) return false;
    Search.AppendChar(Character); RebuildCatalogue(); return true;
}

bool ABridgeNativeHUD::HandleKey(FKey Key) {
    auto* Control = NativeController(); auto* Contents = Inventory(); if (!Control || !Contents) return false;
    if (Key == EKeys::F3) { DebugVisible = !DebugVisible; return true; }
    if (!Control->IsInventoryOpen()) return false;
    if (Control->GetNativeReceiver() && Control->GetNativeReceiver()->IsNativeSaving()) return true;
    if (Key == EKeys::BackSpace && !HasSearchFocus() && !Search.IsEmpty()) { Search.LeftChopInline(1); RebuildCatalogue(); return true; }
    if (HasSearchFocus()) return false;
    const FKey Numbers[] = {EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
    for (int32 I = 0; I < 9; ++I) if (Key == Numbers[I]) {
        for (const auto& Hit : SlotHits) if (Within(Hit.Bounds, Pointer)) {
            if (!Hit.CatalogueItem.IsEmpty()) return Contents->AssignHotbar(Hit.CatalogueItem, I, Contents->MaxCount(Hit.CatalogueItem));
            if (Hit.Slot >= 0 && Contents->GetCursor().IsEmpty()) {
                return Contents->SwapSlots(Hit.Slot, I);
            }
        }
        Contents->SelectHotbar(I); return true;
    }
    return false;
}

void ABridgeNativeHUD::EnsureSearchWidget() {
    if (SearchOverlay || !GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized()) return;
    SearchViewport = GEngine->GameViewport;
    SearchOverlay = SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
        // Overlay slot padding is a value, not a bound Slate attribute in UE 5.8.
        // SBox owns the dynamic padding so resize/GUI scale still move the field.
        [ SNew(SBox)
        .Padding_Lambda([this] { return FMargin(SearchWidgetBounds.Min.X, SearchWidgetBounds.Min.Y, 0, 0); })
        [ SNew(SBox).WidthOverride_Lambda([this] { return SearchWidgetBounds.GetSize().X; }).HeightOverride_Lambda([this] { return SearchWidgetBounds.GetSize().Y; })
          [ SAssignNew(SearchField, SEditableTextBox)
            .Text(FText::FromString(Search)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .SelectAllTextWhenFocused(false).ClearKeyboardFocusOnCommit(false)
            .OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Event) {
                if (Event.GetKey() == EKeys::Escape) { if (auto* Control = NativeController()) Control->ToggleInventory(); return FReply::Handled(); }
                return FReply::Unhandled();
            })
            .OnTextChanged_Lambda([this](const FText& Value) { Search = Value.ToString().Left(128); RebuildCatalogue(); }) ] ] ];
    SearchOverlay->SetVisibility(EVisibility::Collapsed); SearchViewport->AddViewportWidgetContent(SearchOverlay.ToSharedRef(), 30);
}

bool ABridgeNativeHUD::HasSearchFocus() const { return SearchField && SearchField->HasKeyboardFocus(); }

void ABridgeNativeHUD::UpdateSearchWidget(bool Visible) {
    if (Visible) EnsureSearchWidget();
    if (!SearchOverlay) return;
    float Scale = 1;
    if (SearchViewport.IsValid()) { const auto ViewportWidget = SearchViewport->GetGameViewportWidget(); if (ViewportWidget) Scale = FMath::Max(.1f, ViewportWidget->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale()); }
    SearchWidgetBounds = FBox2D(SearchBounds.Min / Scale, SearchBounds.Max / Scale);
    SearchOverlay->SetVisibility(Visible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
    if (!Visible && HasSearchFocus() && FSlateApplication::IsInitialized()) FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
}

void ABridgeNativeHUD::EndPlay(const EEndPlayReason::Type Reason) {
    if (SearchViewport.IsValid() && SearchOverlay) SearchViewport->RemoveViewportWidgetContent(SearchOverlay.ToSharedRef());
    SearchOverlay.Reset(); SearchField.Reset(); Super::EndPlay(Reason);
}
