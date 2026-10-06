#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
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
    void SetInteractionWorld(class ABridgeWorld* Imported);
    void SwingHand() { SwingRemaining=.22f; }
    UPROPERTY(BlueprintReadOnly,Category="Bridge") bool UEAuthority=false;
    bool PreviousJump=false;
    void ApplyMinecraftPose(double BodyHeight, double EyeHeight, bool Sneak);
    UPROPERTY(BlueprintReadOnly, Category="Bridge") bool BridgeSneaking = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCameraComponent> BridgeCamera;
private:
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Sleeve;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Hand;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> HeldMesh;
    UPROPERTY() TObjectPtr<class USceneComponent> AimRoot;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> AimEdges;
    UPROPERTY() TObjectPtr<class UMaterialInterface> VisualMaterial;
    UPROPERTY() TObjectPtr<class UBridgeBlockPalette> VisualPalette;
    UPROPERTY() TObjectPtr<class ABridgeWorld> InteractionWorld;
    FString VisualItem, VisualBlock;
    int32 VisualColor=-1;
    bool VisualsConfigured=false;
    float BobPhase=0, SwingRemaining=0;
};
