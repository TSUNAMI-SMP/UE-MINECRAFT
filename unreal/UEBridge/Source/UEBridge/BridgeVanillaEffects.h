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
    void SpawnBreak(const FVector& Center,const FString& BlockId,FColor Tint);
    int32 ParticleCount() const { return Particles.Num(); }
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
    FString FindGroup(const FString& BlockId,FColor Tint);
    void AddParticle(const FVector& Position,const FVector& Velocity,const FString& Group);
    void SpawnSprint(const FVector& Feet,const FVector& Velocity,const FString& BlockId,FColor Tint);
    void ClearParticles();
};
