#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BridgeMobData.generated.h"

/** A one-shot entity snapshot. IDs are tombstoned for the lifetime of the world import. */
struct FBridgeMobSnapshot {
    FString Id, Type, Appearance;
    FVector Position=FVector::ZeroVector;
    double Yaw=0;
    float Width=.6f, Height=1.8f, Health=20, MaxHealth=20, Speed=.25f, Damage=0, KnockbackResistance=0, Armor=0, ArmorToughness=0;
    bool Hostile=false, Baby=false;
};

/** Geometry/poses are captured from the user's active Minecraft models locally. */
USTRUCT(BlueprintType)
struct FBridgeMobPart {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FString Name;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 Parent=-1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FVector> Vertices;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FVector2D> Texcoords;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FTransform Rest=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FTransform> WalkFrames;
};
USTRUCT(BlueprintType)
struct FBridgeMobAppearance {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FString Key;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FString Type;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Width=.6f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Height=1.8f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float MaxHealth=20;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Speed=.25f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Damage=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float KnockbackResistance=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Armor=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float ArmorToughness=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool Hostile=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool Baby=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<class UMaterialInterface> Material;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector RenderScale=FVector::OneVector;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector RenderOffset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FBridgeMobPart> Parts;
};
UCLASS(BlueprintType)
class UEBRIDGE_API UBridgeMobPalette : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Mobs") TArray<FBridgeMobAppearance> Appearances;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bridge|Mobs") TMap<FString,FString> Templates;
    const FBridgeMobAppearance* Find(const FString& Key) const {
        for(const FBridgeMobAppearance& Appearance:Appearances) if(Appearance.Key==Key) return &Appearance;
        return nullptr;
    }
};
