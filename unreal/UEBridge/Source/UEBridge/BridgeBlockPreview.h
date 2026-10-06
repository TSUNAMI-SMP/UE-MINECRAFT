#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeProtocol.h"
#include "BridgeBlockPreview.generated.h"

/** Visual-only, bounded block snapshot. Never collides with the player/Chaos walls. */
UCLASS()
class UEBRIDGE_API ABridgeBlockPreview : public AActor {
    GENERATED_BODY()
public:
    ABridgeBlockPreview();
    void Replace(const TArray<FBridgeBlock>& Blocks, const FVector& Anchor, class UMaterialInterface* Material, class UBridgeBlockPalette* Palette=nullptr);
    void Clear();
private:
    UPROPERTY() TObjectPtr<class UStaticMesh> Cube;
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> Groups;
};
