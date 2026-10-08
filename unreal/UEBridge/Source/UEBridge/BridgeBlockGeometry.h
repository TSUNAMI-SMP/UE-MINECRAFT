#pragma once
#include "CoreMinimal.h"

/** Baked resource-pack quad. Coordinates are Minecraft-local blocks, not UE cm. */
struct FBridgeModelFace {
    FVector Vertices[4];
    FVector2D UV[4];
    FString TextureId;
    bool bTint=false;
    int32 TintIndex=-1;
    /** Render-only bias for ordered coplanar layers; lighting and hulls use native vertices. */
    FVector RenderOffset=FVector::ZeroVector;
    FColor Color=FColor::White;
    FVector NativeNormal=FVector::ZeroVector;
    bool HasNativeNormal=false;
    /** Native cullface is independent of the normal (rotated cuboids may have none). */
    FIntVector CullOffset=FIntVector::ZeroValue;
};
