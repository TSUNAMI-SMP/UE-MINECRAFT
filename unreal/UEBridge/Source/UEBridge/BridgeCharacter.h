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
    void ConfigureVisuals(class UMaterialInterface* Material,class UBridgeBlockPalette* Palette,const FString& Item,const FString& Block,int32 Color,const FString& ModelKey=FString());
    /** Native inventory display only; offhand use actions are not simulated here. */
    void ConfigureOffhandVisuals(const FString& Item,const FString& Block,int32 Color,const FString& ModelKey=FString());
    void ConfigureAppearance(class UBridgePlayerAppearance* Appearance);
    void ApplyPlayerVisuals(int32 Perspective,float SwingProgress,float EquipProgress,bool UsingItem,const FString& UseAction,float UseProgress,bool LeftHanded,int32 SkinLayers,bool SlimArms);
    /** Local equip progress and imported appearance settings, without a 20 Hz MC pose lease. */
    void ApplyNativePresentation(int32 Perspective,bool LeftHanded,int32 SkinLayers,bool SlimArms,float DeltaSeconds);
    void SetNativeUse(bool Using,float Progress,const FString& Action=TEXT("bow"));
    /** Gameplay always aims from the eyes, also when the display camera is in third person. */
    void GetEyeAim(FVector& EyePosition,FRotator& AimRotation) const;
    /** Minecraft feet are on the floor; UE keeps the physical capsule slightly above it. */
    FVector GetMinecraftFeetPosition() const;
    bool IsAuthoritySprinting() const;
    void SetMinecraftFov(float VerticalFov);
    void ConfigureNativeViewOptions(bool BobView,float FovEffectScale);
    void SetInteractionWorld(class ABridgeWorld* Imported);
    /** Centimeters; legacy bridge mode keeps its 5-block outline range. */
    void SetNativeBlockReach(float ReachCm);
    void SwingHand() { if(SwingRemaining<=.15f) SwingRemaining=.30f; }
    void ApplyFlight(bool Creative,bool Flying);
    void ConfigureOutline(class UMaterialInterface* Material);
    UPROPERTY(BlueprintReadOnly,Category="Bridge|Diagnostics") bool BridgeFlying=false;
    UPROPERTY(BlueprintReadOnly,Category="Bridge|Diagnostics") FString HeldModelStatus=TEXT("empty");
    UPROPERTY(BlueprintReadOnly,Category="Bridge|Diagnostics") FString OffhandModelStatus=TEXT("empty");
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
    bool FlightWasAirborne=false,FlightLandingLatch=false;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Sleeve;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Hand;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> HeldMesh;
    UPROPERTY() TObjectPtr<class USceneComponent> AimRoot;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> AimOutline;
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
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> OffhandModel;
    // Small immutable model sections. The projected vertices are rebuilt from
    // these sources, never from the previous frame's already deformed geometry.
    struct FHandSection {
        TArray<FVector> Positions,Normals;
        TArray<FVector2D> UV;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
    };
    TMap<UProceduralMeshComponent*,TArray<FHandSection>> HandSources;
    FIntVector AimVoxel=FIntVector::ZeroValue;FString AimState;bool AimShapeReady=false;
    bool HeldGeometryReady=false,NativeHeldGeometry=false;
    bool OffhandGeometryReady=false,NativeOffhandGeometry=false,OffhandPendingVisual=false;
    float OffhandEquip=1;
    FString OffhandItem,OffhandBlock,OffhandModelKey,PendingOffhandItem,PendingOffhandBlock,PendingOffhandModelKey;
    int32 OffhandColor=0xffffff,PendingOffhandColor=0xffffff;
    FString VisualItem, VisualBlock, VisualModelKey;
    int32 VisualColor=-1;
    bool VisualsConfigured=false;
    float BobPhase=0, SwingRemaining=0, LimbAmplitude=0, HandBob=0;
    float MinecraftBaseFov=80, FovSprintMultiplier=1;
    bool NativeBobView=true;
    float NativeFovEffectScale=1;
    float BlockOutlineReachCm=500;
    bool SprintRequested=false,BodyYawInitialized=false;
    int32 CameraPerspective=0,PlayerSkinLayers=127;
    float PlayerSwing=0,PlayerEquip=1,PlayerUseProgress=0,RemoteEyeHeight=162;
    bool NativePresentation=false,NativeEquipLowering=false;
    bool NativeUsingItem=false;
    float NativeUseProgress=0;
    FString NativeUseAction=TEXT("none");
    bool NativePendingVisual=false,NativeApplyingVisual=false;
    UPROPERTY() TObjectPtr<class UMaterialInterface> NativePendingMaterial;
    UPROPERTY() TObjectPtr<class UBridgeBlockPalette> NativePendingPalette;
    FString NativePendingItem,NativePendingBlock,NativePendingModel;
    int32 NativePendingColor=-1;
    bool PlayerUsingItem=false,PlayerLeftHanded=false,PlayerSlim=false,HasPlayerVisuals=false,AvatarGeometryReady=false;
    FString PlayerUseAction;
    void BuildAvatarGeometry();
    void BuildHeldGeometry();
    void BuildHandGeometry(UProceduralMeshComponent* Model,const FString& Item,const FString& Block,int32 Color,const FString& ModelKey,bool LeftHanded,bool& NativeGeometry,FString& Status);
    void CacheHandGeometry(UProceduralMeshComponent* Part);
    void PoseHandGeometry(UProceduralMeshComponent* Part,const FTransform& Pose,bool FixedHandFov);
    void UpdatePlayerCamera();
    void UpdateAvatar(float Bob);
    float GetFloorGapCm() const;
    float GetHandSwing() const;
};
