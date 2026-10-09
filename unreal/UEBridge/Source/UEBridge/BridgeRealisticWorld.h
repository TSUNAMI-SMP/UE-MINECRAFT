#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "BridgeRealisticPhysics.h"
#include "BridgeRealisticWorld.generated.h"

/** UE-only extension. No Minecraft process, socket or premium physics plugin. */
UCLASS()
class UEBRIDGE_API ABridgeRealisticWorld : public AActor {
    GENERATED_BODY()
public:
    ABridgeRealisticWorld();
    virtual void Tick(float DeltaSeconds) override;
    void Initialize(class ABridgeWorld* Terrain,class UBridgeNativeUiPalette* Resources);
    bool Use(const FString& Item,const FVector& Eye,const FRotator& Aim,bool Collect,FString& Result);
    void SetVisuals(bool Enabled,int32 Quality);
    bool VisualsEnabled() const {return Realistic;}
    int32 GetQuality() const {return QualityLevel;}
    FString Command(const TArray<FString>& Args,const FVector& Player);
    TSharedPtr<class FJsonObject> ExportState() const;
    bool ImportState(const TSharedPtr<class FJsonObject>& State);
    int32 FluidAt(const FVector& Position) const;
    TArray<FVector> CollisionAnchors() const;
    FString Statistics() const;
    void RenderNow(double Clock=-1);
    BridgeRealistic::Simulation Physics;
    bool ReplayActive=false;
    TFunction<void(const FVector&,float)> OnBlast;
private:
    bool Realistic=true;
    bool VisualWasEmpty=true;
    int32 QualityLevel=1;
    double Accumulator=0,VisualClock=0;
    TSharedPtr<FJsonObject> UndoClear;
    FCollisionQueryParams TerrainQuery;
    UPROPERTY() TObjectPtr<class ABridgeWorld> Terrain;
    UPROPERTY() TObjectPtr<class UBridgeNativeUiPalette> Resources;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> SandMesh;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> TntMesh;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> RockMesh;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> SplashMesh;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> WaterMesh;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> LavaMesh;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> Materials;
    void BuildFluid(int32 Kind,class UProceduralMeshComponent* Mesh,double Clock);
    void RefreshCollisionQuery();
};
