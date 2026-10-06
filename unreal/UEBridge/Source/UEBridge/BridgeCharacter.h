#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ProceduralMeshComponent.h"
#include "BridgeCharacter.generated.h"

UCLASS()
class UEBRIDGE_API ABridgeCharacter : public ACharacter {
    GENERATED_BODY()
public:
    ABridgeCharacter();
    virtual void Tick(float DeltaSeconds) override;
    void SetAuthorityEnabled(bool Enabled);
    void ApplyUEInput(float Forward,float Right,bool JumpHeld,bool Sneak,bool Sprint=false);
    void ConfigureVisuals(class UMaterialInterface* Material,class UBridgeBlockPalette* Palette,const FString& Item,const FString& Block,int32 Color);
    void ConfigureAppearance(class UBridgePlayerAppearance* Appearance);
    void ApplyPlayerVisuals(int32 Perspective,float SwingProgress,float EquipProgress,bool UsingItem,const FString& UseAction,float UseProgress,bool LeftHanded,int32 SkinLayers,bool SlimArms);
    /** Gameplay always aims from the eyes, also when the display camera is in third person. */
    void GetEyeAim(FVector& EyePosition,FRotator& AimRotation) const;
    /** Minecraft feet are on the floor; UE keeps the physical capsule slightly above it. */
    FVector GetMinecraftFeetPosition() const;
    bool IsAuthoritySprinting() const;
    void SetMinecraftFov(float VerticalFov);
    void SetInteractionWorld(class ABridgeWorld* Imported);
    void SwingHand() { SwingRemaining=.30f; }
    UPROPERTY(BlueprintReadOnly,Category="Bridge") bool UEAuthority=false;
    bool PreviousJump=false;
    void ApplyMinecraftPose(double BodyHeight, double EyeHeight, bool Sneak);
    UPROPERTY(BlueprintReadOnly, Category="Bridge") bool BridgeSneaking = false;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") bool BridgeSprinting = false;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") float BridgeEyeHeightCm = 162;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") float BridgeFloorGapCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") float BridgeVerticalFov = 80;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") float BridgeHandVerticalFov = 70;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Diagnostics") float BridgeBodyYaw = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCameraComponent> BridgeCamera;
protected:
    virtual bool CanJumpInternal_Implementation() const override;
private:
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Sleeve;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Hand;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> HeldMesh;
    UPROPERTY() TObjectPtr<class USceneComponent> AimRoot;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> AimEdges;
    UPROPERTY() TObjectPtr<class UMaterialInterface> VisualMaterial;
    UPROPERTY() TObjectPtr<class UBridgeBlockPalette> VisualPalette;
    UPROPERTY() TObjectPtr<class ABridgeWorld> InteractionWorld;
    UPROPERTY() TObjectPtr<class UBridgePlayerAppearance> PlayerAppearance;
    UPROPERTY() TObjectPtr<class USceneComponent> AvatarRoot;
    UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> AvatarParts;
    UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> AvatarLayers;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> SkinArm;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> SkinSleeve;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> ProjectedSleeve;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> ProjectedHand;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> HeldModel;
    // Small immutable model sections. The projected vertices are rebuilt from
    // these sources, never from the previous frame's already deformed geometry.
    struct FHandSection {
        TArray<FVector> Positions,Normals;
        TArray<FVector2D> UV;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
    };
    TMap<UProceduralMeshComponent*,TArray<FHandSection>> HandSources;
    bool HeldGeometryReady=false;
    FString VisualItem, VisualBlock;
    int32 VisualColor=-1;
    bool VisualsConfigured=false;
    float BobPhase=0, SwingRemaining=0, LimbAmplitude=0;
    float MinecraftBaseFov=80, FovSprintMultiplier=1;
    bool SprintRequested=false,BodyYawInitialized=false;
    int32 CameraPerspective=0,PlayerSkinLayers=127;
    float PlayerSwing=0,PlayerEquip=1,PlayerUseProgress=0,RemoteEyeHeight=162;
    bool PlayerUsingItem=false,PlayerLeftHanded=false,PlayerSlim=false,HasPlayerVisuals=false,AvatarGeometryReady=false;
    FString PlayerUseAction;
    void BuildAvatarGeometry();
    void BuildHeldGeometry();
    void CacheHandGeometry(UProceduralMeshComponent* Part);
    void PoseHandGeometry(UProceduralMeshComponent* Part,const FTransform& Pose,bool FixedHandFov);
    void UpdatePlayerCamera();
    void UpdateAvatar(float Bob);
    float GetFloorGapCm() const;
    float GetHandSwing() const;
};
