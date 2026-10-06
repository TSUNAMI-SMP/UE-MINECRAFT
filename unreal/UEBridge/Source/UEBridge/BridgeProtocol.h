#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EBridgeKind { Input, Tnt, Bow, Snapshot, ClearPreview, WorldCell, WorldScope, WorldClear, WorldBegin, WorldCommit, VideoConfig, BlockAction, FeedbackAck };
struct FBridgeBlock {
    FVector Position = FVector::ZeroVector; // Minecraft-relative block centers.
    int32 Color = 0;
    FVector Size = FVector::OneVector;
    FString BlockId;
    bool Collision=false;
    FIntVector SourceBlock=FIntVector::ZeroValue;
    bool HasSourceBlock=false;
};
struct FBridgePacket {
    EBridgeKind Kind = EBridgeKind::Input;
    FString Session, EventId, SnapshotId;
    uint64 Sequence = 0, SnapshotSequence = 0;
    FVector Position = FVector::ZeroVector, Direction = FVector::ZeroVector;
    double Yaw = 0, Pitch = 0, Forward = 0, Right = 0, Pull = 0;
    bool Jump = false;
    bool Sneak = false, Controller=false;
    FString ImportId, Action, HeldItem, HeldBlock;
    int32 HeldColor=0xffffff;
    bool Sprint=false;
    int32 Perspective=0, SkinLayers=127;
    double SwingProgress=0, EquipProgress=1, UseProgress=0, CameraFov=70;
    bool UsingItem=false, LeftHanded=false, SlimArms=false;
    FString UseAction=TEXT("none");
    FVector MinecraftOrigin=FVector::ZeroVector;
    int32 ImportCells=0;
    double EyeHeight = 1.62, BodyHeight = 1.8;
    FIntVector Cell = FIntVector::ZeroValue;
    int32 Radius = 2, HalfHeight = 1;
    int32 VideoWidth=960, VideoHeight=540, VideoFps=20, VideoQuality=85;
    double VideoExposure=0;
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
        return FeetAnchor + FVector(Minecraft.Z, -Minecraft.X, Minecraft.Y) * 100.0;
    }
    inline FVector ToDirection(const FVector& Minecraft) {
        return FVector(Minecraft.Z, -Minecraft.X, Minecraft.Y);
    }
    inline FRotator ToRotation(double MinecraftYaw, double MinecraftPitch) {
        return FRotator(-MinecraftPitch, MinecraftYaw, 0);
    }
}
