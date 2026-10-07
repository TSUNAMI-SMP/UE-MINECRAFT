#pragma once
#include "CoreMinimal.h"

/** Baked resource-pack quad. Coordinates are Minecraft-local blocks, not UE cm. */
struct FBridgeModelFace {
    FVector Vertices[4];
    FVector2D UV[4];
    FString TextureId;
    bool bTint=false;
    FColor Color=FColor::White;
    /** Native cullface is independent of the normal (rotated cuboids may have none). */
    FIntVector CullOffset=FIntVector::ZeroValue;
};
