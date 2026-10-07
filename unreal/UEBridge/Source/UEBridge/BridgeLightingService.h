#pragma once
#include "CoreMinimal.h"
#include "BridgeLightingMath.h"
class FJsonObject;
class UWorld;
class AActor;
class UBridgeBlockPalette;

/** Game-thread facade. Mesh vertices carry light levels, not final tinted colours. */
class UEBRIDGE_API FBridgeLightingService {
public:
    bool Reset(const FIntVector& Minimum,const FIntVector& Maximum,bool HasSky=true);
    void Clear();
    void SetVoxel(const FIntVector& Voxel,uint8 Opacity,uint8 Emission,uint8 NativeSky=15,uint8 NativeBlock=0,uint8 SolidFaces=0);
    void ClearVoxel(const FIntVector& Voxel);
    void SetSkyBoundary(int32 X,int32 Z,uint8 Light);
    void Initialize();
    void BeginInitialize();
    uint32 Tick(uint32 Budget=200000);
    int32 Pending() const;
    TSet<FIntVector> ConsumeChangedCells();
    void DiscardChanged();
    FLinearColor Vertex(const FIntVector& Voxel,const FVector& LocalMinecraft,const FVector& NormalMinecraft) const;
    FLinearColor Sample(const FIntVector& Voxel) const;
    /** Numerical field state at one voxel; no GPU readback or full-field scan. */
    FString Describe(const FIntVector& Voxel) const;
    /** Read exported per-state lighting; never guess lamp state from its name. */
    static bool StateProperties(const UBridgeBlockPalette* Palette,const FString& BlockId,const FString& State,uint8& Opacity,uint8& Emission);
    static uint8 StateFaceMask(const UBridgeBlockPalette* Palette,const FString& BlockId,const FString& State);
    /** Shared shader environment, affecting only Bridge generated materials in this world. */
    static bool SetEnvironment(UWorld* World,const TSharedPtr<FJsonObject>& Values,bool Vanilla,FString* FailureReason=nullptr);
    /** Apply sampled light to arms/items/mobs/drops without rebuilding their geometry. */
    void ApplyActor(AActor* Actor,const FIntVector& Voxel) const;
private:
    BridgeLightingMath::Field Field;
    FIntVector DirtyMinCell=FIntVector::ZeroValue,DirtyCellSize=FIntVector::ZeroValue;
};
