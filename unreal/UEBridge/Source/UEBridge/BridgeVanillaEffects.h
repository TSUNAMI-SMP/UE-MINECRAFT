#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BridgeVanillaEffects.generated.h"

/** Local outcome metadata; the Minecraft client selects its active vanilla sound. */
struct FBridgeVanillaEvent {
    FString Type, BlockId;
    FIntVector SourceVoxel=FIntVector::ZeroValue;
    FColor Tint=FColor::White;
    FVector Position=FVector::ZeroVector;
    float FallDistance=0;
};

/** Bounded counters since Play; these report CPU submission, not verified GPU visibility. */
struct FBridgeDustDiagnostics {
    bool MaterialReady=false;
    int32 TextureCount=0, Active=0, Instances=0, PeakInstances=0, Groups=0;
    uint64 Requested=0, Spawned=0, Rejected=0;
    FString Reason=TEXT("missing_material"), LastType, LastBlock, LastReason=TEXT("none");
    int32 LastRequested=0, LastSpawned=0;
    float SizeMultiplier=.75f,DensityMultiplier=1.f,LifetimeMultiplier=.9f;
};

/** Terrain-textured dust, with Minecraft's 20 Hz lifetime/drag and UE collision. */
UCLASS()
class UEBRIDGE_API ABridgeVanillaEffects : public AActor {
    GENERATED_BODY()
public:
    ABridgeVanillaEffects();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Configure(class ABridgeWorld* ImportedTerrain,class UBridgeBlockPalette* ImportedPalette,class UMaterialInterface* Material);
    void SetViewCamera(class UCameraComponent* Camera);
    void SampleCharacter(class ABridgeCharacter* Character,float DeltaSeconds,TArray<FBridgeVanillaEvent>& OutEvents);
    void ResetMovement();
    void SpawnBreak(const FVector& Center,const FString& BlockId,FColor Tint,const TArray<FBox>& MinecraftShapeBoxes=TArray<FBox>());
    void ConfigureParticleTuning(float SizeMultiplier,float DensityMultiplier,float LifetimeMultiplier);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Bridge|Particles",meta=(ClampMin="0.25",ClampMax="2.0")) float ParticleSizeMultiplier=.75f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Bridge|Particles",meta=(ClampMin="0.0",ClampMax="1.0")) float ParticleDensityMultiplier=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Bridge|Particles",meta=(ClampMin="0.25",ClampMax="2.0")) float ParticleLifetimeMultiplier=.9f;
    int32 ParticleCount() const { return Particles.Num(); }
    FBridgeDustDiagnostics GetDiagnostics() const;
private:
    struct FDustParticle {
        FString Group;
        FVector Previous=FVector::ZeroVector, Position=FVector::ZeroVector, Velocity=FVector::ZeroVector;
        FVector2D TextureOffset=FVector2D::ZeroVector;
        float Size=10;
        int32 Age=0, Lifetime=10;
    };
    UPROPERTY() TObjectPtr<class ABridgeWorld> Terrain;
    UPROPERTY() TObjectPtr<class UBridgeBlockPalette> Palette;
    UPROPERTY() TObjectPtr<class UMaterialInterface> DustMaterial;
    UPROPERTY() TObjectPtr<class UStaticMesh> ParticlePlane;
    UPROPERTY() TMap<FString,TObjectPtr<class UInstancedStaticMeshComponent>> Groups;
    TWeakObjectPtr<class UCameraComponent> ViewCamera;
    TWeakObjectPtr<class ABridgeCharacter> SampledCharacter;
    TArray<FDustParticle> Particles;
    bool HaveMovementSample=false, WasGrounded=false;
    FVector PreviousFeet=FVector::ZeroVector;
    float WalkDistance=0, FallPeak=0, SprintClock=0, PhysicsClock=0;
    double SprintDensityAccumulator=0;
    FBridgeDustDiagnostics Diagnostics;
    bool Configured=false;
    double LastFailureLog=-1;
    bool ResolveTexture(const FString& BlockId,FColor& Tint,class UTexture*& Texture) const;
    void BeginRequest(const FString& Type,const FString& BlockId,int32 Count);
    void Reject(int32 Count,const FString& Reason);
    FString FindGroup(const FString& BlockId,FColor Tint);
    bool AddParticle(const FVector& Position,const FVector& Velocity,const FString& Group);
    void SpawnSprint(const FVector& Feet,const FVector& Velocity,const FString& BlockId,FColor Tint);
    void ClearParticles();
};
