#include "BridgeNativeHUD.h"
#include "BridgeNativeInventory.h"
#include "BridgeNativePlayerController.h"
#include "BridgeReceiver.h"
#include "BridgeVideo.h"
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
    if (Texture) {
        // Container PNGs include unused atlas space and widgets. Draw the panel
        // region at its native pixel size instead of shrinking the entire atlas.
        const bool Container=Key==TEXT("container/inventory") || Key==TEXT("container/creative_inventory/tab_items") || Key==TEXT("container/creative_inventory/tab_inventory") || Key==TEXT("container/creative_inventory/tab_item_search")
            || (Resources && Resources->Groups.ContainsByPredicate([&](const FBridgeNativeUiGroup& Group) {return Group.Texture==Key;}));
        const float U=Container ? FMath::Min(1.f,W/256.f) : 1.f;
        const float V=Container ? FMath::Min(1.f,H/256.f) : 1.f;
        DrawTexture(Texture, X * GuiScale, Y * GuiScale, W * GuiScale, H * GuiScale, 0, 0, U, V, Color, BLEND_Translucent);
    }
}

void ABridgeNativeHUD::SpriteRegion(const FString& Key, float X, float Y, float W, float H, float U, float V, float UW, float VH) {
    if (UTexture2D* Texture = Resources ? Resources->FindSprite(Key) : nullptr)
        DrawTexture(Texture, X * GuiScale, Y * GuiScale, W * GuiScale, H * GuiScale, U, V, UW, VH, FLinearColor::White, BLEND_Translucent);
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
    const auto& Offhand = Contents->GetOffhand();
    if (!Offhand.IsEmpty()) {
        const auto* Control = NativeController();
        const bool RightSide = Control && Control->IsNativeLeftHanded();
        const float OffhandX = RightSide ? X + 182 : X - 29;
        const FString Backing = RightSide ? TEXT("hud/hotbar_offhand_right") : TEXT("hud/hotbar_offhand_left");
        if (Resources && Resources->FindSprite(Backing)) Sprite(Backing, OffhandX, Y - 1, 29, 24);
        else { Solid(OffhandX, Y - 1, 29, 24, FLinearColor(.1f, .1f, .1f, .8f)); Solid(OffhandX + (RightSide ? 10 : 3), Y + 3, 16, 16, FLinearColor(.35f, .35f, .35f)); }
        Item(Offhand.ItemId, Offhand.Count, OffhandX + (RightSide ? 10 : 3), Y + 3);
    }
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
    if(const auto* Contents=Inventory()) {
        const int32 Armor=FMath::CeilToInt(Contents->GetArmorPoints());
        if(Armor>0) for(int32 I=0;I<10;++I) Sprite(Armor-I*2>=2 ? TEXT("hud/armor_full") : (Armor-I*2==1 ? TEXT("hud/armor_half") : TEXT("hud/armor_empty")),X+I*8,Y-10,9,9);
    }
    for (int32 I = 0; I < 10; ++I) {
        Sprite(TEXT("hud/heart/container"), X + I * 8, Y, 9, 9);
        const int32 Heart = HalfHearts - I * 2;
        if (Heart > 0) Sprite(Heart == 1 ? TEXT("hud/heart/half") : TEXT("hud/heart/full"), X + I * 8, Y, 9, 9);
    }
}

const FBridgeNativeUiGroup* ABridgeNativeHUD::CurrentGroup() const { return Groups.IsValidIndex(SelectedGroup) ? &Groups[SelectedGroup] : nullptr; }

void ABridgeNativeHUD::RebuildGroups() {
    Groups = Resources ? Resources->Groups : TArray<FBridgeNativeUiGroup>();
    if (Groups.IsEmpty()) {
        // Existing exports do not contain creative categories. Preserve their inventory without inventing order.
        FBridgeNativeUiGroup SearchGroup; SearchGroup.GroupId=TEXT("minecraft:search"); SearchGroup.DisplayName=TEXT("アイテム検索"); SearchGroup.Type=TEXT("search"); SearchGroup.Column=6; SearchGroup.Special=true; SearchGroup.IconItem=TEXT("minecraft:compass"); SearchGroup.Texture=TEXT("container/creative_inventory/tab_item_search");
        if (Resources) for (const auto& Entry : Resources->Items) SearchGroup.Items.Add(Entry.ItemId);
        Groups.Add(SearchGroup);
        FBridgeNativeUiGroup Own; Own.GroupId=TEXT("minecraft:inventory"); Own.DisplayName=TEXT("サバイバルインベントリ"); Own.Type=TEXT("inventory"); Own.Row=1; Own.Column=6; Own.Special=true; Own.Scrollbar=false; Own.IconItem=TEXT("minecraft:chest"); Own.Texture=TEXT("container/creative_inventory/tab_inventory"); Groups.Add(Own);
    }
    SelectedGroup=0; Search.Reset(); RebuildCatalogue();
}

void ABridgeNativeHUD::RebuildCatalogue() {
    auto* Contents=Inventory(); const auto* Group=CurrentGroup();
    CatalogueMode=!Group || Group->Type!=TEXT("inventory");
    Catalogue=Contents ? Contents->FilterCatalogue(Group && Group->Type==TEXT("search") ? Search : FString(),Group) : TArray<FBridgeNativeUiItem>(); CatalogueRow=0;
}

void ABridgeNativeHUD::DrawPlayerPreview(float X, float Y, float Scale) {
    const auto* Control = NativeController(); const auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr;
    if (!Receiver || !Receiver->PlayerAppearance || !Receiver->PlayerAppearance->SkinMaterial) return;
    UTexture* Skin = nullptr;
    if (!Receiver->PlayerAppearance->SkinMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("SkinTexture")), Skin) || !Skin) return;
    auto Piece = [&](float SX, float SY, float SW, float SH, float DX, float DY, float DW, float DH) {
        DrawTexture(Skin, (X + DX * Scale) * GuiScale, (Y + DY * Scale) * GuiScale, DW * Scale * GuiScale, DH * Scale * GuiScale, SX / 64, SY / 64, SW / 64, SH / 64, FLinearColor::White, BLEND_Translucent);
    };
    Piece(8, 8, 8, 8, 8, 0, 16, 16); Piece(40, 8, 8, 8, 7.5f, -.5f, 17, 17);
    Piece(20, 20, 8, 12, 8, 16, 16, 24); Piece(20, 36, 8, 12, 8, 16, 16, 24);
    const float Arm = Receiver->PlayerAppearance->IsSlim ? 6 : 8;
    Piece(44, 20, Receiver->PlayerAppearance->IsSlim ? 3 : 4, 12, 8 - Arm, 16, Arm, 24);
    Piece(36, 52, Receiver->PlayerAppearance->IsSlim ? 3 : 4, 12, 24, 16, Arm, 24);
    Piece(4, 20, 4, 12, 8, 40, 8, 24); Piece(20, 52, 4, 12, 16, 40, 8, 24);
}

void ABridgeNativeHUD::DrawInventory() {
    auto* Contents=Inventory(); const auto* Control=NativeController(); const auto* Receiver=Control ? Control->GetNativeReceiver() : nullptr; if (!Contents || !Receiver) return;
    Solid(0,0,GuiWidth,GuiHeight,FLinearColor(0,0,0,.62f)); SlotHits.Reset(); TabHits.Reset(); Tooltip.Reset();
    const bool Creative=Receiver->NativeCreative; const auto* Group=CurrentGroup();
    const bool OwnInventory=Creative && Group && Group->Type==TEXT("inventory");
    const bool SearchTab=Creative && Group && Group->Type==TEXT("search");
    const float W=Creative ? 195 : 176,H=Creative ? 136 : 166;
    const float X=FMath::FloorToFloat((GuiWidth-W)/2),Y=FMath::FloorToFloat((GuiHeight-H)/2);
    PanelBounds=FBox2D(FVector2D(X,Y)*GuiScale,FVector2D(X+W,Y+H)*GuiScale);
    DeleteBounds=SearchBounds=ScrollBounds=FBox2D(ForceInit);
    auto Tab=[&](int32 Index) {
        const auto& TabGroup=Groups[Index]; const bool Top=TabGroup.Row==0,Selected=Index==SelectedGroup;
        const float TX=X+(TabGroup.Special ? W-27*(7-TabGroup.Column)+1 : 27*TabGroup.Column),TY=Y+(Top ? -28 : H-4);
        const FString SpriteKey=FString::Printf(TEXT("container/creative_inventory/tab_%s_%s_%d"),Top ? TEXT("top") : TEXT("bottom"),Selected ? TEXT("selected") : TEXT("unselected"),TabGroup.Column+1);
        Sprite(SpriteKey,TX,TY,26,32); Item(TabGroup.IconItem,1,TX+5,TY+(Top ? 9 : 7));
        FTabHit Hit; Hit.Group=Index; Hit.Bounds=FBox2D(FVector2D(TX,Y+(Top ? -32 : H))*GuiScale,FVector2D(TX+26,Y+(Top ? 0 : H+32))*GuiScale); TabHits.Add(Hit);
        if (Within(Hit.Bounds,Pointer)) Tooltip=TabGroup.DisplayName;
    };
    if (Creative) for (int32 I=0;I<Groups.Num();++I) if(I!=SelectedGroup) Tab(I);
    Sprite(Creative && Group ? Group->Texture : TEXT("container/inventory"),X,Y,W,H);
    auto DrawSlot=[&](int32 Index,float SX,float SY,const FString& CatalogueId=FString(),bool IsCatalogue=false) {
        FSlotHit Hit; Hit.Bounds=FBox2D(FVector2D(SX,SY)*GuiScale,FVector2D(SX+16,SY+16)*GuiScale); Hit.Slot=Index; Hit.CatalogueItem=CatalogueId;Hit.CatalogueSlot=IsCatalogue;SlotHits.Add(Hit);
        FString Id=CatalogueId;int32 Count=IsCatalogue ? 1 : 0;
        if(!IsCatalogue) {
            const auto& Stack=Contents->GetStack(Index);Id=Stack.ItemId;Count=Stack.Count;
            if(DragActive && DragSlots.Num()>1 && DragSlots.Contains(Index)) {
                Id=DragItem;
                const int32 Share=DragButton==EKeys::MiddleMouseButton ? Contents->SlotCapacity(Index,DragItem) : (DragButton==EKeys::RightMouseButton ? 1 : DragCount/DragSlots.Num());
                Count=FMath::Min(Count+Share,Contents->SlotCapacity(Index,DragItem));
            }
        }
        if(Id.IsEmpty()) {
            const TCHAR* ArmorIcons[]={TEXT("helmet"),TEXT("chestplate"),TEXT("leggings"),TEXT("boots")};
            if(Index>=UBridgeNativeInventory::ArmorBegin && Index<UBridgeNativeInventory::ArmorEnd) Sprite(FString(TEXT("container/slot/"))+ArmorIcons[Index-UBridgeNativeInventory::ArmorBegin],SX,SY,16,16);
            else if(Index==UBridgeNativeInventory::OffhandSlot) Sprite(TEXT("container/slot/shield"),SX,SY,16,16);
        }
        Item(Id,Count,SX,SY);
        if(Within(Hit.Bounds,Pointer)) {Solid(SX,SY,16,16,FLinearColor(1,1,1,.3f));const auto* Entry=Resources ? Resources->FindItem(Id) : nullptr;Tooltip=Entry ? Entry->DisplayName : Id;}
    };
    if(Creative) {
        if(OwnInventory) {
            // CreativeInventoryScreen.setSelectedTab: storage at y54, hotbar y112, offhand35/20, bin173/112.
            DrawPlayerPreview(X+79,Y+6,.65f);
            for(int32 Row=0;Row<3;++Row) for(int32 Column=0;Column<9;++Column) DrawSlot(9+Row*9+Column,X+9+Column*18,Y+54+Row*18);
            DrawSlot(UBridgeNativeInventory::OffhandSlot,X+35,Y+20);
            for(int32 I=0;I<4;++I) DrawSlot(UBridgeNativeInventory::ArmorBegin+I,X+54+(I/2)*54,Y+6+(I%2)*27);
            DeleteBounds=FBox2D(FVector2D(X+173,Y+112)*GuiScale,FVector2D(X+189,Y+128)*GuiScale);
            if(Within(DeleteBounds,Pointer)) {Solid(X+173,Y+112,16,16,FLinearColor(1,1,1,.3f));Tooltip=TEXT("アイテムを削除");}
        } else {
            if(Group && Group->RenderName) Text(Group->DisplayName,X+8,Y+6,FLinearColor(.25f,.25f,.25f),false);
            if(SearchTab) {
                SearchBounds=FBox2D(FVector2D(X+82,Y+6)*GuiScale,FVector2D(X+171,Y+17)*GuiScale);
                FString VisibleSearch=Search;
                while(!VisibleSearch.IsEmpty() && TextWidth(VisibleSearch)>86) VisibleSearch.RightChopInline(1);
                Text(VisibleSearch,X+82,Y+6,FLinearColor::White,false);
                if(HasSearchFocus() && FMath::Fmod(FPlatformTime::Seconds(),1)<.5) Solid(X+82+TextWidth(VisibleSearch),Y+6,1,9,FLinearColor::White);
            }
            for(int32 Row=0;Row<5;++Row) for(int32 Column=0;Column<9;++Column) {
                const int32 Index=(CatalogueRow+Row)*9+Column;DrawSlot(-1,X+9+Column*18,Y+18+Row*18,Catalogue.IsValidIndex(Index) ? Catalogue[Index].ItemId : FString(),true);
            }
            const int32 Rows=FMath::Max(0,FMath::DivideAndRoundUp(Catalogue.Num(),9)-5);
            if(!Group || Group->Scrollbar) {
                ScrollBounds=FBox2D(FVector2D(X+175,Y+18)*GuiScale,FVector2D(X+187,Y+108)*GuiScale);
                Sprite(Rows>0 ? TEXT("container/creative_inventory/scroller") : TEXT("container/creative_inventory/scroller_disabled"),X+175,Y+18+(Rows>0 ? 75.f*CatalogueRow/Rows : 0),12,15);
            }
        }
        for(int32 I=0;I<9;++I) DrawSlot(I,X+9+I*18,Y+112);
        if(Groups.IsValidIndex(SelectedGroup)) Tab(SelectedGroup);
    } else {
        DrawPlayerPreview(X+33,Y+12);
        Text(TEXT("クラフト"),X+97,Y+6,FLinearColor(.25f,.25f,.25f),false);
        for(int32 Row=0;Row<3;++Row) for(int32 Column=0;Column<9;++Column) DrawSlot(9+Row*9+Column,X+8+Column*18,Y+84+Row*18);
        for(int32 I=0;I<9;++I) DrawSlot(I,X+8+I*18,Y+142);
        DrawSlot(UBridgeNativeInventory::OffhandSlot,X+77,Y+62);
        for(int32 I=0;I<4;++I) DrawSlot(UBridgeNativeInventory::ArmorBegin+I,X+8,Y+8+I*18);
    }
    if(!Tooltip.IsEmpty() && Contents->GetCursor().IsEmpty()) {
        const float TX=FMath::Min(Pointer.X/GuiScale+12,GuiWidth-TextWidth(Tooltip)-6),TY=Pointer.Y/GuiScale-12;
        Solid(TX-3,TY-3,TextWidth(Tooltip)+6,14,FLinearColor(.06f,.01f,.12f,.96f));Text(Tooltip,TX,TY);
    }
    if(!Contents->GetCursor().IsEmpty()) {
        int32 ShownCount=Contents->GetCursor().Count;
        if(DragActive && DragSlots.Num()>1) for(int32 Slot:DragSlots) {
            const auto& Stack=Contents->GetStack(Slot);const int32 Current=Stack.IsEmpty() ? 0 : Stack.Count;
            const int32 Share=DragButton==EKeys::MiddleMouseButton ? Contents->SlotCapacity(Slot,DragItem) : (DragButton==EKeys::RightMouseButton ? 1 : DragCount/DragSlots.Num());
            ShownCount-=FMath::Min(Current+Share,Contents->SlotCapacity(Slot,DragItem))-Current;
        }
        if(ShownCount>0) Item(Contents->GetCursor().ItemId,ShownCount,Pointer.X/GuiScale-8,Pointer.Y/GuiScale-8);
    }
    UpdateDragSlots();
    UpdateSearchWidget(SearchTab);
}

void ABridgeNativeHUD::DrawCrosshair() {
    const auto* Control=NativeController();auto* Receiver=Control ? Control->GetNativeReceiver() : nullptr;
    if(!Receiver) return;
    auto* Video=Receiver->Video.Get();
    if(Video) for(int32 I=0;I<3;++I) Video->SetNativeInverseSprite(I,nullptr,FVector4(0,0,0,0),FVector4(0,0,1,1),false);
    if(!Control->NativeHudVisible || Control->IsInventoryOpen() || Control->IsPausedMenuOpen()) return;
    auto Inverse=[&](int32 Slot,const FString& Key,float X,float Y,float W,float H,float U=0,float V=0,float UW=1,float VH=1) {
        UTexture2D* Texture=Resources ? Resources->FindSprite(Key) : nullptr;
        if(!Texture || W<=0 || H<=0) return;
        const bool Applied=Video && Video->SetNativeInverseSprite(Slot,Texture,FVector4(X*GuiScale,Y*GuiScale,W*GuiScale,H*GuiScale),FVector4(U,V,UW,VH),true);
        if(!Applied) SpriteRegion(Key,X,Y,W,H,U,V,UW,VH);
    };
    const float Charge=Receiver->GetNativeAttackCharge();
    if(Control->NativePerspective==0) {
        Inverse(0,TEXT("hud/crosshair"),FMath::FloorToFloat((GuiWidth-15)/2),FMath::FloorToFloat((GuiHeight-15)/2),15,15);
        if(Control->GetNativeAttackIndicator()==1) {
            const float X=FMath::FloorToFloat(GuiWidth/2)-8,Y=FMath::FloorToFloat(GuiHeight/2)+9;
            if(Charge>=1 && Receiver->GetNativeAttackCooldownTicks()>5 && Receiver->IsNativeAttackTargetAlive()) Inverse(1,TEXT("hud/crosshair_attack_indicator_full"),X,Y,16,16);
            else if(Charge<1) {
                Inverse(1,TEXT("hud/crosshair_attack_indicator_background"),X,Y,16,4);
                const float Width=FMath::FloorToFloat(Charge*17);
                Inverse(2,TEXT("hud/crosshair_attack_indicator_progress"),X,Y,Width,4,0,0,Width/16,1);
            }
        }
    }
    if(Control->GetNativeAttackIndicator()==2 && Charge<1) {
        const float X=FMath::FloorToFloat(GuiWidth/2)+(Control->IsNativeLeftHanded() ? -113 : 97),Y=GuiHeight-20;
        const float Height=FMath::FloorToFloat(Charge*19);
        Sprite(TEXT("hud/hotbar_attack_indicator_background"),X,Y,18,18);
        if(Height>0) SpriteRegion(TEXT("hud/hotbar_attack_indicator_progress"),X,Y+18-Height,18,Height,0,(18-Height)/18,1,Height/18);
    }
}

void ABridgeNativeHUD::DrawPauseMenu() {
    Solid(0, 0, GuiWidth, GuiHeight, FLinearColor(0, 0, 0, .65f)); PauseButtons.Reset();
    const FString Title = TEXT("ゲームメニュー"); Text(Title, (GuiWidth - TextWidth(Title)) / 2, GuiHeight / 2 - 115);
    const auto* Control = NativeController(); const auto* Receiver = Control ? Control->GetNativeReceiver() : nullptr;
    const FString Labels[] = {TEXT("ゲームに戻る"), TEXT("ワールドを保存"), TEXT("開始地点に戻る"),
        Receiver && Receiver->NativeLighting ? TEXT("照明: ON") : TEXT("照明: OFF"), TEXT("終了"),
        FString::Printf(TEXT("感度を下げる  %.0f%%"),Control ? Control->GetNativeSensitivity()*200 : 100),
        TEXT("感度を上げる"),TEXT("視点を切り替える")};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Labels); ++I) {
        const float X = GuiWidth / 2 - 100, Y = GuiHeight / 2 - 88 + I * 23;
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
    if (Resources != Contents->GetPalette()) { Resources = Contents->GetPalette(); RebuildGroups(); }
    UpdateGuiScale(); float MX = 0, MY = 0; if (GetOwningPlayerController()->GetMousePosition(MX, MY)) Pointer = FVector2D(MX, MY);
    if (Control->IsInventoryOpen()) {
        if (!WasInventoryOpen) { RebuildCatalogue(); WasInventoryOpen = true; }
        DrawInventory();
    } else {
        WasInventoryOpen = false; SlotHits.Reset();CancelDrag(); UpdateSearchWidget(false);
        if (Control->IsPausedMenuOpen()) DrawPauseMenu();
        else if (Control->NativeHudVisible) { DrawHotbar(); DrawHealth(); }

    }
    DrawCrosshair();
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
    if (!Control || !Contents || !Receiver || (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton && Button != EKeys::MiddleMouseButton)) return false;
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
            else if (I == 5) Control->SetNativeSensitivity(Control->GetNativeSensitivity()-.025f);
            else if (I == 6) Control->SetNativeSensitivity(Control->GetNativeSensitivity()+.025f);
            else if (I == 7) Control->CycleNativePerspective();
            return true;
        }
        return true;
    }
    if (!Control->IsInventoryOpen()) return false;
    CancelDrag();
    if(Button==EKeys::MiddleMouseButton) {
        if(Receiver->NativeCreative) for(const auto& Hit : SlotHits) if(Within(Hit.Bounds,Position)) {
            if(Contents->GetCursor().IsEmpty()) {
                const FString Id=Hit.CatalogueSlot ? Hit.CatalogueItem : Contents->GetStack(Hit.Slot).ItemId;if(!Id.IsEmpty()) Contents->TakeCatalogue(Id);
            } else if(!Hit.CatalogueSlot && Hit.Slot>=0) {
                DragActive=true;DragButton=Button;DragItem=Contents->GetCursor().ItemId;DragCount=Contents->GetCursor().Count;DragStartSlot=Hit.Slot;UpdateDragSlots();
            }
            break;
        }
        return true;
    }
    if(Receiver->NativeCreative) for(const auto& Hit : TabHits) if(Within(Hit.Bounds,Position)) {
        if(SelectedGroup!=Hit.Group) {SelectedGroup=Hit.Group;Search.Reset();if(SearchField) SearchField->SetText(FText::GetEmpty());RebuildCatalogue();UpdateSearchWidget(false);}
        return true;
    }
    const bool Shift = Control->IsInputKeyDown(EKeys::LeftShift) || Control->IsInputKeyDown(EKeys::RightShift);
    if(Receiver->NativeCreative && Within(DeleteBounds,Position)) {Contents->DeleteCreative(Shift);return true;}
    if(Receiver->NativeCreative && Button==EKeys::LeftMouseButton && Within(ScrollBounds,Position)) {
        const int32 Rows=FMath::Max(0,FMath::DivideAndRoundUp(Catalogue.Num(),9)-5);
        const float Relative=FMath::Clamp((Position.Y-ScrollBounds.Min.Y-7.5f*GuiScale)/(75*GuiScale),0.f,1.f);
        CatalogueRow=FMath::RoundToInt(Relative*Rows);return true;
    }
    if (CatalogueMode && Within(SearchBounds, Position)) { EnsureSearchWidget(); if (SearchField && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(SearchField, EFocusCause::Mouse); return true; }
    const bool RightClick = Button == EKeys::RightMouseButton;
    for (const auto& Hit : SlotHits) if (Within(Hit.Bounds, Position)) {
        if (Hit.CatalogueSlot) Contents->ClickCatalogue(Hit.CatalogueItem,RightClick,Shift);
        else if(Hit.Slot>=0) {
            if(!Shift && !Contents->GetCursor().IsEmpty()) {
                CancelDrag();DragActive=true;DragButton=Button;DragItem=Contents->GetCursor().ItemId;DragCount=Contents->GetCursor().Count;DragStartSlot=Hit.Slot;UpdateDragSlots();
            } else Contents->ClickSlot(Hit.Slot,RightClick,Shift);
        }
        return true;
    }
    if (!Within(PanelBounds, Position) && !Contents->GetCursor().IsEmpty()) {
        const FString ItemId = Contents->GetCursor().ItemId;
        const int32 Count = RightClick ? 1 : Contents->GetCursor().Count;
        if (Receiver->NativeDrop(ItemId, Count)) { FBridgeNativeStack Dropped; Contents->TakeCursor(Count, Dropped); }
    }
    return true;
}

void ABridgeNativeHUD::CancelDrag() {DragActive=false;DragSlots.Reset();DragStartSlot=-1;DragItem.Reset();DragCount=0;}
void ABridgeNativeHUD::UpdateDragSlots() {
    if(!DragActive) return;
    auto* Contents=Inventory();if(!Contents || Contents->GetCursor().ItemId!=DragItem || Contents->GetCursor().Count!=DragCount) {CancelDrag();return;}
    for(const auto& Hit:SlotHits) if(!Hit.CatalogueSlot && Hit.Slot>=0 && Within(Hit.Bounds,Pointer)) {
        const auto& Stack=Contents->GetStack(Hit.Slot);
        if(!DragSlots.Contains(Hit.Slot) && (DragButton==EKeys::MiddleMouseButton || DragCount>DragSlots.Num()) && Contents->CanInsertIntoSlot(Hit.Slot,DragItem) && (Stack.IsEmpty() || Stack.ItemId==DragItem)) DragSlots.Add(Hit.Slot);
        break;
    }
}
bool ABridgeNativeHUD::HandlePointerReleased(FKey Button,FVector2D Position) {
    if(!DragActive || Button!=DragButton) return false;
    Pointer=Position;UpdateDragSlots();if(!DragActive) return true;
    auto* Contents=Inventory();const int32 Start=DragStartSlot;const TArray<int32> Targets=DragSlots;
    const auto* Control=NativeController();const auto* Receiver=Control ? Control->GetNativeReceiver() : nullptr;
    CancelDrag();if(!Contents || !Control || !Control->IsInventoryOpen()) return true;
    if(Targets.Num()>1) Contents->DistributeCursor(Targets,Button==EKeys::MiddleMouseButton ? 2 : (Button==EKeys::RightMouseButton ? 1 : 0),Receiver && Receiver->NativeCreative);
    else if(Start>=0 && Button!=EKeys::MiddleMouseButton) Contents->ClickSlot(Start,Button==EKeys::RightMouseButton);
    return true;
}

bool ABridgeNativeHUD::HandleScroll(int32 Delta) {
    const auto* Control = NativeController(); if (!Control || !Control->IsInventoryOpen() || !CatalogueMode) return false;
    const int32 Maximum = FMath::Max(0, FMath::DivideAndRoundUp(Catalogue.Num(), 9) - 5); CatalogueRow = FMath::Clamp(CatalogueRow - Delta, 0, Maximum); return true;
}

bool ABridgeNativeHUD::HandleText(TCHAR Character) {
    const auto* Control = NativeController(); if (!Control || !Control->IsInventoryOpen() || !CurrentGroup() || CurrentGroup()->Type!=TEXT("search") || !Within(SearchBounds, Pointer) || HasSearchFocus()) return false;
    if (Character < ' ' || Character == 127 || Search.Len() >= 128) return false;
    Search.AppendChar(Character); RebuildCatalogue(); return true;
}

bool ABridgeNativeHUD::HandleKey(FKey Key) {
    auto* Control = NativeController(); auto* Contents = Inventory(); if (!Control || !Contents) return false;
    if (Key == EKeys::F3) { DebugVisible = !DebugVisible; return true; }
    if (!Control->IsInventoryOpen()) return false;
    if (Key == EKeys::BackSpace && !HasSearchFocus() && !Search.IsEmpty()) { Search.LeftChopInline(1); RebuildCatalogue(); return true; }
    if (HasSearchFocus()) return false;
    if(Control->MatchesBinding(TEXT("key.drop"),Key)) {
        auto* Receiver=Control->GetNativeReceiver();
        const bool Shift=Control->IsInputKeyDown(EKeys::LeftControl) || Control->IsInputKeyDown(EKeys::RightControl);
        if(Receiver) for(const auto& Hit : SlotHits) if(Within(Hit.Bounds,Pointer)) {
            if(Hit.CatalogueSlot) {
                if(Receiver->NativeCreative && !Hit.CatalogueItem.IsEmpty()) Receiver->NativeDrop(Hit.CatalogueItem,Shift ? Contents->MaxCount(Hit.CatalogueItem) : 1);
            } else if(Hit.Slot>=0) {
                const auto& Stack=Contents->GetStack(Hit.Slot);const int32 Count=Shift ? Stack.Count : 1;
                if(!Stack.IsEmpty() && Receiver->NativeDrop(Stack.ItemId,Count)) {FBridgeNativeStack Dropped;Contents->TakeSlot(Hit.Slot,Count,Dropped);}
            }
            break;
        }
        return true;
    }
    if (Control->MatchesBinding(TEXT("key.swapOffhand"), Key)) {
        for (const auto& Hit : SlotHits) if (Within(Hit.Bounds, Pointer) && Hit.Slot >= 0)
            return Contents->SwapSlots(Hit.Slot, UBridgeNativeInventory::OffhandSlot);
        return true;
    }
    for (int32 I = 0; I < 9; ++I) if (Control->MatchesBinding(FString::Printf(TEXT("key.hotbar.%d"), I + 1), Key)) {
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
        [ SNew(SBox).Visibility(EVisibility::SelfHitTestInvisible)
        .Padding_Lambda([this] { return FMargin(SearchWidgetBounds.Min.X, SearchWidgetBounds.Min.Y, 0, 0); })
        [ SNew(SBox).Visibility(EVisibility::SelfHitTestInvisible).WidthOverride_Lambda([this] { return SearchWidgetBounds.GetSize().X; }).HeightOverride_Lambda([this] { return SearchWidgetBounds.GetSize().Y; })
          [ SAssignNew(SearchField, SEditableTextBox)
            .Text(FText::FromString(Search)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .SelectAllTextWhenFocused(false).ClearKeyboardFocusOnCommit(false)
            .ForegroundColor(FLinearColor::Transparent).BackgroundColor(FLinearColor::Transparent).BorderBackgroundColor(FLinearColor::Transparent).Padding(FMargin(0))
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
    if(auto* Control=NativeController()) if(auto* Receiver=Control->GetNativeReceiver()) if(auto* Video=Receiver->Video.Get())
        for(int32 I=0;I<3;++I) Video->SetNativeInverseSprite(I,nullptr,FVector4(0,0,0,0),FVector4(0,0,1,1),false);
    SearchOverlay.Reset(); SearchField.Reset(); Super::EndPlay(Reason);
}
