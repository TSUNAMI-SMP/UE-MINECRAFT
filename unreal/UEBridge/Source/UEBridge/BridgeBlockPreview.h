#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeProtocol.h"
#include "BridgeBlockPreview.generated.h"

/** Bounded block cells; optional solid collision for UE-owned initial imports. */
UCLASS()
class UEBRIDGE_API ABridgeBlockPreview : public AActor {
    GENERATED_BODY()
public:
    ABridgeBlockPreview();
    void Replace(const TArray<FBridgeBlock>& Blocks, const FVector& Anchor, class UMaterialInterface* Material, class UBridgeBlockPalette* Palette=nullptr, bool Physics=false);
    void Clear();
private:
    UPROPERTY() TObjectPtr<class UStaticMesh> Cube;
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> Groups;
};
