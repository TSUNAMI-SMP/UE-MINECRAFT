#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "BridgeMobData.h"

enum class EBridgeKind { Input, Tnt, Bow, Snapshot, ClearPreview, WorldCell, WorldScope, WorldClear, WorldBegin, WorldCommit, VideoConfig, BlockAction, FeedbackAck, MobSpawn, MobClear, MobTemplateSpawn, PlayerRespawn, ItemDrop, ItemResolve };
struct FBridgeBlock {
    FVector Position = FVector::ZeroVector; // Minecraft-relative block centers.
    int32 Color = 0;
    FVector Size = FVector::OneVector;
    FString BlockId;
    bool Collision=false;
    FIntVector SourceBlock=FIntVector::ZeroValue;
    bool HasSourceBlock=false;
    FString StateKey;
    uint8 Role=0; // 0 legacy, 1 baked render model, 2 collision, 3 outline/selection.
    uint8 SkyLight=15,BlockLight=0,Emission=0,Opacity=15;
    bool HasLight=false,NativeCompact=false;
};
struct FBridgePacket {
    EBridgeKind Kind = EBridgeKind::Input;
    FString Session, EventId, SnapshotId;
    uint64 Sequence = 0, SnapshotSequence = 0;
    FVector Position = FVector::ZeroVector, Direction = FVector::ZeroVector;
    double Yaw = 0, Pitch = 0, Forward = 0, Right = 0, Pull = 0;
    bool Jump = false;
    bool Sneak = false, Controller=false;
    FString ImportId, Action, HeldItem, HeldBlock, SpawnType, HeldModelKey;
    int32 HeldColor=0xffffff;
    bool Sprint=false,Creative=false,Flying=false,ItemSession=false,CompactTerrain=false;
    int32 Perspective=0, SkinLayers=127;
    double SwingProgress=0, EquipProgress=1, UseProgress=0, CameraFov=80;
    bool UsingItem=false, LeftHanded=false, SlimArms=false;
    FString UseAction=TEXT("none");
    FVector MinecraftOrigin=FVector::ZeroVector;
    int32 ImportCells=0;
    double EyeHeight = 1.62, BodyHeight = 1.8;
    FIntVector Cell = FIntVector::ZeroValue;
    int32 Radius = 2, HalfHeight = 1;
    int32 VideoWidth=960, VideoHeight=540, VideoFps=20, VideoQuality=85;
    double VideoExposure=0;
    bool Lighting=true, VanillaSky=false;
    double ParticleScale=.75,ParticleDensity=1,ParticleLifetime=.9;
    FBridgeMobSnapshot Mob;
    TSharedPtr<FJsonObject> VanillaLight;
    TArray<uint8> SkyTop;
    /** Optional offline-only grass, foliage and dry foliage RGB at all 512 source voxels. */
    TArray<FIntVector> BiomeTints;
    TArray<uint16> Water;
    FString ItemTx,ItemId,ItemModelKey,ItemEpoch;
    int32 ItemCount=0,ItemMaxCount=64,ItemRevision=0,ItemAccepted=0;
    FVector ItemPosition=FVector::ZeroVector,ItemVelocity=FVector::ZeroVector;
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
