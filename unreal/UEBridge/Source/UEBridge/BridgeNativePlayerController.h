#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "BridgeNativeLookMath.h"
#include "BridgeNativePlayerController.generated.h"

/** Direct UE input. Fabric remains available for imports and legacy bridge mode. */
UCLASS()
class UEBRIDGE_API ABridgeNativePlayerController : public APlayerController {
    GENERATED_BODY()
public:
    ABridgeNativePlayerController();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    void ConfigureNativeSettings(const TSharedPtr<class FJsonObject>& Settings);
    bool RestoreNativeInventory(const TSharedPtr<class FJsonObject>& State);
    bool IsSavedInventoryValid() const {return !bSavedInventoryRejected;}
    FString GetInventoryRestoreError() const {return InventoryRestoreError;}
    void RestoreNativeView(float Yaw,float Pitch,bool Flying,int32 Perspective);
    class UBridgeNativeInventory* GetNativeInventory() const {return NativeInventory;}
    class ABridgeReceiver* GetNativeReceiver() const {return NativeReceiver;}
    bool IsInventoryOpen() const {return bInventoryOpen;}
    bool IsPausedMenuOpen() const {return bPauseOpen;}
    int32 GetNativeGuiScale() const {return GuiScale;}
    bool GetNativeForceUnicode() const {return ForceUnicode;}
    UFUNCTION(BlueprintCallable,Category="Bridge|Native") void ToggleInventory();
    UFUNCTION(BlueprintCallable,Category="Bridge|Native") void TogglePause();
    UFUNCTION(BlueprintCallable,Category="Bridge|Native") void SelectNativeHotbar(int32 Slot);
    FString BindingLabel(const FString& Action) const;
    bool MatchesBinding(const FString& Action, const FKey& Key) const;
    bool IsNativeLeftHanded() const {return LeftHanded;}
    float GetNativeSensitivity() const {return MouseSensitivity;}
    int32 GetNativeAttackIndicator() const {return AttackIndicator;}
    void SetNativeSensitivity(float Value);
    void CycleNativePerspective();
    UPROPERTY(BlueprintReadOnly,Category="Bridge|Native") int32 NativePerspective=0;
    UPROPERTY(BlueprintReadOnly,Category="Bridge|Native") bool NativeHudVisible=true;
    UPROPERTY(BlueprintReadOnly,Category="Bridge|Native") FString NativeInputStatus;
private:
    UPROPERTY() TObjectPtr<class ABridgeReceiver> NativeReceiver;
    UPROPERTY() TObjectPtr<class UBridgeNativeInventory> NativeInventory;
    UPROPERTY() TObjectPtr<class UBridgeNativeUiPalette> LoadedUiPalette;
    TMap<FString,FKey> Bindings;
    bool bInventoryOpen=false,bPauseOpen=false,bWasNative=false,bSettingsConfigured=false,bInventoryInitialized=false;
    bool bLastFocused=true,bFlying=false,bDoubleSprint=false,bToggleSneak=false,bToggleSprint=false;
    bool bFlightApplied=false,bBowHeld=false;
    bool ToggleCrouchOption=false,ToggleSprintOption=false,InvertMouse=false,InvertMouseX=false,LeftHanded=false,SlimArms=false;
    bool BobView=true;
    bool SmoothCamera=false;
    int32 AttackIndicator=1;
    BridgeNativeLookMath::Smoother LookXSmoother,LookYSmoother;
    int32 SkinLayers=127,GuiScale=0;
    bool ForceUnicode=false;
    bool bSavedInventoryRejected=false;
    FString InventoryRestoreError;
    float MouseSensitivity=.5f,NativeBaseFov=70.f,NativeFovEffectScale=1.f;
    double MouseWheelSensitivity=1,WheelRemainder=0;
    double PreviousJumpTap=-1,PreviousForwardTap=-1,NextAttack=0,NextUse=0;
    FString SelectedItem,SettingsProfile=TEXT("default");
    TSharedPtr<class FJsonObject> CachedNativeSettings;
    TSharedPtr<class FJsonObject> PendingSavedInventory;
    int32 SelectedSlot=-1;
    void ResetDefaultBindings();
    void FindReceiver();
    bool Down(const FString& Action) const;
    bool Pressed(const FString& Action) const;
    bool Focused() const;
    void SetMenuInput();
    void StopNativeInput();
    void UpdateSelectedItem();
    void InitializeNativeInventory();
    bool ApplySavedNativeInventory();
    void RouteMenuInput();
    static FKey MinecraftKey(const FString& TranslationKey);
};
