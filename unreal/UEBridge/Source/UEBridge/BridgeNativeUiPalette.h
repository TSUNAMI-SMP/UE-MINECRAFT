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
