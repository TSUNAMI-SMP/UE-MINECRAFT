#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BridgeNativeUiPalette.generated.h"

/** Resource-pack UI assets are exported by Fabric and imported locally, never bundled. */
USTRUCT(BlueprintType)
struct FBridgeNativeUiItem {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString ItemId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<class UTexture2D> Icon;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxCount = 64;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString BlockId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString ModelKey;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString SpawnType;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 EquipmentSlot = 0; // 1head,2chest,3legs,4feet,0none.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Armor = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ArmorToughness = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ArmorKnockbackResistance = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackDamage = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackSpeed = 0; // 0: legacy export, use vanilla fallback.
};

/** ItemGroups.getDisplayStacks ordering captured from the user's running Minecraft. */
USTRUCT(BlueprintType)
struct FBridgeNativeUiGroup {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString GroupId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Type;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString IconItem;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Texture;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Row = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Column = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool Special = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool Scrollbar = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool RenderName = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FString> Items;
};

USTRUCT(BlueprintType)
struct FBridgeNativeGlyph {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Codepoint = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 X = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Y = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Width = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Height = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Advance = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float DrawWidth = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float DrawHeight = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Ascent = 7;
};

UCLASS(BlueprintType)
class UEBRIDGE_API UBridgeNativeUiPalette : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") TMap<FString, TObjectPtr<class UTexture2D>> Sprites;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") TArray<FBridgeNativeUiItem> Items;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") TArray<FBridgeNativeUiGroup> Groups;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") TArray<TObjectPtr<class UTexture2D>> DeathPoofFrames;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") TObjectPtr<class UTexture2D> FontAtlas;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") TArray<FBridgeNativeGlyph> Glyphs;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") FString Language;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Bridge|Native UI") FString ExportId;
    const FBridgeNativeUiItem* FindItem(const FString& ItemId) const {
        return Items.FindByPredicate([&](const FBridgeNativeUiItem& Entry) { return Entry.ItemId == ItemId; });
    }
    UTexture2D* FindSprite(const FString& Key) const {
        const auto* Found = Sprites.Find(Key); return Found ? Found->Get() : nullptr;
    }
};
