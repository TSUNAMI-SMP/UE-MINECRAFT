#pragma once
#include "CoreMinimal.h"

/** Baked resource-pack quad. Coordinates are Minecraft-local blocks, not UE cm. */
struct FBridgeModelFace {
    FVector Vertices[4];
    FVector2D UV[4];
    FString TextureId;
    bool bTint=false;
    FColor Color=FColor::White;
};
