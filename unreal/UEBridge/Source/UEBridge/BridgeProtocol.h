#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EBridgeKind { Input, Tnt, Bow, Snapshot, ClearPreview };
struct FBridgeBlock {
    FVector Position = FVector::ZeroVector; // Minecraft-relative block centers.
    int32 Color = 0;
};
struct FBridgePacket {
    EBridgeKind Kind = EBridgeKind::Input;
    FString Session, EventId, SnapshotId;
    uint64 Sequence = 0, SnapshotSequence = 0;
    FVector Position = FVector::ZeroVector, Direction = FVector::ZeroVector;
    double Yaw = 0, Pitch = 0, Forward = 0, Right = 0, Pull = 0;
    bool Jump = false;
    int32 BatchIndex = 0, TotalBatches = 0;
    TArray<FBridgeBlock> Blocks;
};

namespace BridgeProtocol {
    constexpr int32 MaxPacketBytes = 2048;
    constexpr int32 MaxPreviewBlocks = 2601;
    constexpr int32 MaxBatches = 217;
    /** Validate the entire packet before renewing a session lease or mutating UE state. */
    bool Parse(const TSharedPtr<FJsonObject>& Json, FBridgePacket& Out);
    inline FVector ToUnreal(const FVector& Minecraft, const FVector& FeetAnchor) {
        return FeetAnchor + FVector(Minecraft.Z, Minecraft.X, Minecraft.Y) * 100.0;
    }
}
