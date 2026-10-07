#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeNativeExplosion.generated.h"

/** Bounded local vanilla sprite animation when a custom Niagara system is unset. */
UCLASS()
class UEBRIDGE_API ABridgeNativeExplosion : public AActor {
    GENERATED_BODY()
public:
    ABridgeNativeExplosion();
    static bool Spawn(class UWorld* World, const FVector& Position, class UBridgeNativeUiPalette* Palette);
    virtual void Tick(float DeltaSeconds) override;
private:
    bool Initialize(class UBridgeNativeUiPalette* Palette, class UMaterialInterface* SpriteMaterial);
    void UpdateSprite();
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> SpriteMesh;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> SpriteInstance;
    UPROPERTY() TArray<TObjectPtr<class UTexture2D>> Frames;
    float Age = 0.f;
    int32 CurrentFrame = INDEX_NONE;
};
