#pragma once
#include "CoreMinimal.h"
#include "BridgeProtocol.h"

class ABridgeWorld;
class UMaterialInterface;
class UBridgeBlockPalette;
class IFileHandle;

/** Source positions are absolute Minecraft block coordinates; rotations use Minecraft degrees. */
struct FBridgeNativeWorldMetadata {
    FString PackageId,Dimension,SourceFile,SaveFile;
    FVector Origin=FVector::ZeroVector,Spawn=FVector::ZeroVector;
    double Yaw=0,Pitch=0;
    FIntVector Center=FIntVector::ZeroValue;
    int32 Radius=0,HalfHeight=0,Cells=0;
    TSharedPtr<FJsonObject> Environment,Manifest,RuntimeState;
    TArray<TSharedPtr<FJsonValue>> Mobs;
};

/** Finite offline terrain. Two streaming passes validate before replacing a live world.
 * Saves are generated cell by cell in Saved/NativeWorlds, with atomic file replacement.
 * Terrain and entity state are captured together; incremental serialization reads
 * the immutable snapshot, so gameplay may continue while the file is written.
 */
class UEBRIDGE_API FBridgeNativeWorldStore {
public:
    FBridgeNativeWorldStore();
    ~FBridgeNativeWorldStore();
    bool BeginLoad(const FString& ManifestOrWorldFile,ABridgeWorld* World,const FVector& Anchor,UMaterialInterface* Material,UBridgeBlockPalette* Palette);
    bool BeginSave(ABridgeWorld* World,const FVector& AbsoluteMinecraftFeet,const FRotator& ControlRotation,const TSharedPtr<FJsonObject>& RuntimeState=nullptr);
    void Tick(double BudgetMs=4);
    bool FlushSave(int32 MaxMilliseconds=5000);
    void Cancel();
    bool IsLoading() const;
    bool IsSaving() const;
    bool IsReady() const;
    float GetProgress() const;
    const FString& GetError() const {return Error;}
    FString GetState() const;
    const FBridgeNativeWorldMetadata& GetMetadata() const {return Metadata;}
    uint64 GetSavedMutationSerial() const {return SavedSerial;}
private:
    enum class EState {Idle,Validating,Loading,Ready,Saving,Failed};
    EState State=EState::Idle;
    FBridgeNativeWorldMetadata Metadata;
    FString Error,ActiveFile,TemporaryFile;
    TWeakObjectPtr<ABridgeWorld> Target;
    TWeakObjectPtr<UMaterialInterface> Material;
    TWeakObjectPtr<UBridgeBlockPalette> Palette;
    FVector FeetAnchor=FVector::ZeroVector;
    TUniquePtr<IFileHandle> Reader,Writer;
    TArray<uint8> ReadBuffer;
    int32 ReadCursor=0,ReadCount=0;
    int64 FileSize=0,BytesRead=0;
    int32 Line=0,ProcessedCells=0;
    int64 ProcessedRows=0;
    uint64 PacketSequence=3,SaveSerial=0,SavedSerial=0;
    FString ImportId;
    TSet<FIntVector> SeenCells;
    TSet<FString> ValidatedModels;
    TArray<FIntVector> SaveCells;
    struct FSaveCell { TArray<FBridgeBlock> Rows; TArray<uint8> SkyTop; TArray<uint16> Water; TArray<FIntVector> BiomeTints; };
    TMap<FIntVector,FSaveCell> SaveSnapshot;
    int32 SaveCursor=0;
    TSharedPtr<FJsonObject> SaveHeader;
    FDateTime ReadTimestamp;
    bool OpenReader(const FString& Path);
    bool ReadLine(FString& Out,bool& AtEnd);
    bool ReadHeader(const FString& Text,FBridgeNativeWorldMetadata& Out);
    bool ParseCell(const FString& Text,FBridgePacket& Out,bool CheckModels);
    bool StartApplying();
    bool WriteLine(const TSharedPtr<FJsonObject>& Object);
    bool SaveNextCell();
    void Fail(const FString& Reason);
};
