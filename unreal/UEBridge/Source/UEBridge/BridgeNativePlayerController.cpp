#include "BridgeNativePlayerController.h"
#include "BridgeNativeInputMath.h"
#include "BridgeNativeInventory.h"
#include "BridgeNativeHUD.h"
#include "BridgeNativeUiPalette.h"
#include "BridgeReceiver.h"
#include "BridgeCharacter.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"

ABridgeNativePlayerController::ABridgeNativePlayerController() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    bShouldPerformFullTickWhenPaused=true;
    bEnableClickEvents=false;bEnableMouseOverEvents=false;
    ResetDefaultBindings();
}
void ABridgeNativePlayerController::BeginPlay() {
    Super::BeginPlay();
    NativeInventory=NewObject<UBridgeNativeInventory>(this);
    FindReceiver();
    InitializeNativeInventory();
}
void ABridgeNativePlayerController::EndPlay(const EEndPlayReason::Type Reason) {
    if(NativeInventory && bInventoryInitialized) NativeInventory->SaveProfile();
    if(bPauseOpen) SetPause(false);
    Super::EndPlay(Reason);
}
void ABridgeNativePlayerController::ResetDefaultBindings() {
    Bindings={
        {TEXT("key.forward"),EKeys::W},{TEXT("key.back"),EKeys::S},
        {TEXT("key.left"),EKeys::A},{TEXT("key.right"),EKeys::D},
        {TEXT("key.jump"),EKeys::SpaceBar},{TEXT("key.sneak"),EKeys::LeftShift},
        {TEXT("key.sprint"),EKeys::LeftControl},{TEXT("key.attack"),EKeys::LeftMouseButton},
        {TEXT("key.use"),EKeys::RightMouseButton},{TEXT("key.pickItem"),EKeys::MiddleMouseButton},
        {TEXT("key.inventory"),EKeys::E},{TEXT("key.drop"),EKeys::Q},
        {TEXT("key.togglePerspective"),EKeys::F5},{TEXT("key.toggleGui"),EKeys::F1},
        {TEXT("key.swapOffhand"),EKeys::F}
    };
    const FKey Digits[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
    for(int32 I=0;I<9;++I) Bindings.Add(FString::Printf(TEXT("key.hotbar.%d"),I+1),Digits[I]);
}
FKey ABridgeNativePlayerController::MinecraftKey(const FString& TranslationKey) {
    // GLFW indexes side buttons from zero: mouse.3=Mouse4, mouse.4=Mouse5.
    if(TranslationKey.StartsWith(TEXT("key.mouse."))) {
        switch(BridgeNativeInputMath::MouseTranslation(TCHAR_TO_UTF8(*TranslationKey))) {
            case BridgeNativeInputMath::MouseButton::Left:return EKeys::LeftMouseButton;
            case BridgeNativeInputMath::MouseButton::Right:return EKeys::RightMouseButton;
            case BridgeNativeInputMath::MouseButton::Middle:return EKeys::MiddleMouseButton;
            case BridgeNativeInputMath::MouseButton::Side4:return EKeys::ThumbMouseButton;
            case BridgeNativeInputMath::MouseButton::Side5:return EKeys::ThumbMouseButton2;
            default:break;
        }
        return FKey();
    }
    if(!TranslationKey.StartsWith(TEXT("key.keyboard."))) return FKey();
    const FString Name=TranslationKey.Mid(13);
    static const TMap<FString,FKey> Special={
        {TEXT("space"),EKeys::SpaceBar},{TEXT("left.shift"),EKeys::LeftShift},{TEXT("right.shift"),EKeys::RightShift},
        {TEXT("left.control"),EKeys::LeftControl},{TEXT("right.control"),EKeys::RightControl},
        {TEXT("left.alt"),EKeys::LeftAlt},{TEXT("right.alt"),EKeys::RightAlt},
        {TEXT("left.win"),EKeys::LeftCommand},{TEXT("right.win"),EKeys::RightCommand},
        {TEXT("escape"),EKeys::Escape},{TEXT("enter"),EKeys::Enter},{TEXT("tab"),EKeys::Tab},
        {TEXT("backspace"),EKeys::BackSpace},{TEXT("delete"),EKeys::Delete},{TEXT("insert"),EKeys::Insert},
        {TEXT("home"),EKeys::Home},{TEXT("end"),EKeys::End},{TEXT("page.up"),EKeys::PageUp},{TEXT("page.down"),EKeys::PageDown},
        {TEXT("up"),EKeys::Up},{TEXT("down"),EKeys::Down},{TEXT("left"),EKeys::Left},{TEXT("right"),EKeys::Right},
        {TEXT("caps.lock"),EKeys::CapsLock},{TEXT("num.lock"),EKeys::NumLock},{TEXT("scroll.lock"),EKeys::ScrollLock},
        // UE 5.8 has no registered EKeys::PrintScreen. It remains unsupported
        // rather than manufacturing a valid-looking key that never receives input.
        {TEXT("pause"),EKeys::Pause},
        {TEXT("apostrophe"),EKeys::Quote},{TEXT("comma"),EKeys::Comma},{TEXT("minus"),EKeys::Hyphen},
        {TEXT("period"),EKeys::Period},{TEXT("slash"),EKeys::Slash},{TEXT("semicolon"),EKeys::Semicolon},
        {TEXT("equal"),EKeys::Equals},{TEXT("left.bracket"),EKeys::LeftBracket},
        {TEXT("right.bracket"),EKeys::RightBracket},{TEXT("backslash"),EKeys::Backslash},{TEXT("grave.accent"),EKeys::Tilde},
        {TEXT("keypad.add"),EKeys::Add},{TEXT("keypad.subtract"),EKeys::Subtract},{TEXT("keypad.multiply"),EKeys::Multiply},
        {TEXT("keypad.divide"),EKeys::Divide},{TEXT("keypad.decimal"),EKeys::Decimal},{TEXT("keypad.enter"),EKeys::Enter}
    };
    if(const auto* Found=Special.Find(Name)) return *Found;
    if(Name.Len()==1 && Name[0]>=TEXT('a') && Name[0]<=TEXT('z')) return FKey(FName(*Name.ToUpper()));
    const FKey Digits[]={EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
    if(Name.Len()==1 && Name[0]>=TEXT('0') && Name[0]<=TEXT('9')) return Digits[Name[0]-TEXT('0')];
    if(Name.StartsWith(TEXT("keypad.")) && Name.Len()==8 && FChar::IsDigit(Name[7])) {
        const FKey Numpad[]={EKeys::NumPadZero,EKeys::NumPadOne,EKeys::NumPadTwo,EKeys::NumPadThree,EKeys::NumPadFour,EKeys::NumPadFive,EKeys::NumPadSix,EKeys::NumPadSeven,EKeys::NumPadEight,EKeys::NumPadNine};
        return Numpad[Name[7]-TEXT('0')];
    }
    if(Name.StartsWith(TEXT("f")) && Name.Len()<=3) {
        const FKey Function(FName(*Name.ToUpper()));if(Function.IsValid()) return Function;
    }
    return FKey();
}
void ABridgeNativePlayerController::ConfigureNativeSettings(const TSharedPtr<FJsonObject>& Settings) {
    const FString PreviousProfile=SettingsProfile;
    CachedNativeSettings=Settings;
    ResetDefaultBindings();
    ToggleCrouchOption=ToggleSprintOption=InvertMouse=InvertMouseX=LeftHanded=SlimArms=ForceUnicode=false;
    BobView=true;MouseSensitivity=.5f;NativeBaseFov=70.f;NativeFovEffectScale=1.f;GuiScale=0;SkinLayers=127;
    MouseWheelSensitivity=1;WheelRemainder=0;
    if(Settings.IsValid()) {
        const TSharedPtr<FJsonObject>* Keys=nullptr;
        if(Settings->TryGetObjectField(TEXT("keyBindings"),Keys)) {
            for(const auto& Binding:(*Keys)->Values) {
                // JSON uses shared-string keys in UE 5.8; own a FString for our map.
                const FString Action(*Binding.Key);
                FString KeyName;if(!Binding.Value->TryGetString(KeyName) || !Bindings.Contains(Action)) continue;
                const FKey Key=MinecraftKey(KeyName);
                // Unknown/unbound means disabled. Never silently substitute F5 for a side button.
                Bindings.Add(Action,Key);
                if(!Key.IsValid() && !KeyName.EndsWith(TEXT("unknown"))) {
                    UE_LOG(LogTemp,Warning,TEXT("Bridge native input: unsupported binding %s=%s"),*Action,*KeyName);
                }
            }
        }
        double Number=0;
        if(Settings->TryGetNumberField(TEXT("mouseSensitivity"),Number)) MouseSensitivity=FMath::Clamp(float(Number),0.f,1.f);
        if(Settings->TryGetNumberField(TEXT("mouseWheelSensitivity"),Number) && FMath::IsFinite(Number)) MouseWheelSensitivity=FMath::Clamp(Number,0.,100.);
        Settings->TryGetBoolField(TEXT("invertYMouse"),InvertMouse);
        Settings->TryGetBoolField(TEXT("invertXMouse"),InvertMouseX);
        if(!Settings->TryGetBoolField(TEXT("sneakToggled"),ToggleCrouchOption)) Settings->TryGetBoolField(TEXT("toggleCrouch"),ToggleCrouchOption);
        if(!Settings->TryGetBoolField(TEXT("sprintToggled"),ToggleSprintOption)) Settings->TryGetBoolField(TEXT("toggleSprint"),ToggleSprintOption);
        Settings->TryGetBoolField(TEXT("bobView"),BobView);
        if(Settings->TryGetNumberField(TEXT("fovEffectScale"),Number) && FMath::IsFinite(Number)) NativeFovEffectScale=FMath::Clamp(float(Number),0.f,1.f);
        Settings->TryGetBoolField(TEXT("slimArms"),SlimArms);
        Settings->TryGetBoolField(TEXT("forceUnicodeFont"),ForceUnicode);
        if(Settings->TryGetNumberField(TEXT("skinLayers"),Number)) SkinLayers=int32(Number)&127;
        if(Settings->TryGetNumberField(TEXT("guiScale"),Number)) GuiScale=FMath::Clamp(int32(Number),0,16);
        FString MainHand;if(Settings->TryGetStringField(TEXT("mainHand"),MainHand)) LeftHanded=MainHand==TEXT("left");
        FString Profile;if(Settings->TryGetStringField(TEXT("profile"),Profile) && !Profile.IsEmpty()) SettingsProfile=Profile;
        if(Settings->TryGetNumberField(TEXT("perspective"),Number)) NativePerspective=FMath::Clamp(int32(Number),0,2);
        if(Settings->TryGetNumberField(TEXT("fov"),Number)) NativeBaseFov=FMath::Clamp(float(Number),30.f,110.f);
    }
    if(auto* BridgePawn=Cast<ABridgeCharacter>(GetPawn())) {
        BridgePawn->ConfigureNativeViewOptions(BobView,NativeFovEffectScale);
        BridgePawn->SetMinecraftFov(NativeBaseFov);
    }
    bSettingsConfigured=true;
    bToggleSneak=bToggleSprint=bDoubleSprint=false;PreviousJumpTap=PreviousForwardTap=-1;
    if(PreviousProfile!=SettingsProfile) {bSavedInventoryRejected=false;InventoryRestoreError.Reset();}
    if(NativeInventory && bInventoryInitialized && PreviousProfile!=SettingsProfile) {
        NativeInventory->SaveProfile();bInventoryInitialized=false;
    }
    InitializeNativeInventory();
    SelectedSlot=-1;SelectedItem.Empty();
    UE_LOG(LogTemp,Display,TEXT("Bridge native input ready: perspective=%s sensitivity=%.3f invertX=%s invertY=%s bob=%s fovEffects=%.2f"),
        *BindingLabel(TEXT("key.togglePerspective")),MouseSensitivity,InvertMouseX ? TEXT("true") : TEXT("false"),InvertMouse ? TEXT("true") : TEXT("false"),BobView ? TEXT("true") : TEXT("false"),NativeFovEffectScale);
}
void ABridgeNativePlayerController::InitializeNativeInventory() {
    if(!NativeInventory || !bSettingsConfigured || !IsValid(NativeReceiver) || !NativeReceiver->NativeUiPalette) return;
    if(bInventoryInitialized && LoadedUiPalette==NativeReceiver->NativeUiPalette) return;
    if(bInventoryInitialized) NativeInventory->SaveProfile();
    LoadedUiPalette=NativeReceiver->NativeUiPalette;
    NativeInventory->Initialize(LoadedUiPalette,SettingsProfile,false);
    NativeInventory->ImportInitialSettings(CachedNativeSettings);
    bInventoryInitialized=true;SelectedSlot=-1;SelectedItem.Empty();
    ApplySavedNativeInventory();
}
bool ABridgeNativePlayerController::RestoreNativeInventory(const TSharedPtr<FJsonObject>& State) {
    if(!State.IsValid()) return false;
    bSavedInventoryRejected=false;InventoryRestoreError.Reset();
    PendingSavedInventory=State;
    return !bInventoryInitialized || !NativeInventory ? true : ApplySavedNativeInventory();
}
bool ABridgeNativePlayerController::ApplySavedNativeInventory() {
    if(!PendingSavedInventory.IsValid() || !NativeInventory || !bInventoryInitialized) return true;
    const auto Saved=PendingSavedInventory;PendingSavedInventory.Reset();
    if(!NativeInventory->ImportRuntimeState(Saved)) {
        bSavedInventoryRejected=true;
        InventoryRestoreError=TEXT("Saved inventory rejected: ")+NativeInventory->GetLastPersistenceError();
        NativeInputStatus=InventoryRestoreError;
        UE_LOG(LogTemp,Error,TEXT("Bridge native inventory restore failed: %s"),*NativeInventory->GetLastPersistenceError());
        return false;
    }
    SelectedSlot=-1;SelectedItem.Empty();return true;
}
void ABridgeNativePlayerController::RestoreNativeView(float Yaw,float Pitch,bool Flying,int32 Perspective) {
    SetControlRotation(FRotator(float(BridgeNativeInputMath::ClampPitch(Pitch)),FRotator::NormalizeAxis(Yaw),0));
    bFlying=Flying;bFlightApplied=false;NativePerspective=FMath::Clamp(Perspective,0,2);
}
void ABridgeNativePlayerController::FindReceiver() {
    if(IsValid(NativeReceiver)) return;
    for(TActorIterator<ABridgeReceiver> It(GetWorld());It;++It) {NativeReceiver=*It;break;}
}
bool ABridgeNativePlayerController::Down(const FString& Action) const {
    const auto* Key=Bindings.Find(Action);return Key && Key->IsValid() && IsInputKeyDown(*Key);
}
bool ABridgeNativePlayerController::Pressed(const FString& Action) const {
    const auto* Key=Bindings.Find(Action);return Key && Key->IsValid() && WasInputKeyJustPressed(*Key);
}
FString ABridgeNativePlayerController::BindingLabel(const FString& Action) const {
    const auto* Key=Bindings.Find(Action);return Key && Key->IsValid() ? Key->GetDisplayName().ToString() : TEXT("Unbound");
}
bool ABridgeNativePlayerController::Focused() const {
    const auto* ViewportClient=GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    return !ViewportClient || !ViewportClient->Viewport || ViewportClient->Viewport->HasFocus();
}
void ABridgeNativePlayerController::StopNativeInput() {
    if(bBowHeld && IsValid(NativeReceiver)) {NativeReceiver->NativeAction(TEXT("use_cancel"));bBowHeld=false;}
    if(IsValid(NativeReceiver) && NativeReceiver->NativePlayActive)
        NativeReceiver->SetNativeInput(0,0,false,false,false,bFlying,NativePerspective,GetControlRotation().Yaw,GetControlRotation().Pitch);
    if(auto* BridgePawn=Cast<ABridgeCharacter>(GetPawn())) {
        BridgePawn->StopJumping();
        if(bInventoryOpen || bPauseOpen || !Focused()) BridgePawn->GetCharacterMovement()->StopMovementImmediately();
    }
}
void ABridgeNativePlayerController::SetMenuInput() {
    const bool Menu=bInventoryOpen || bPauseOpen;
    bShowMouseCursor=Menu;
    if(Menu) {
        FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
    } else {
        FInputModeGameOnly Mode;Mode.SetConsumeCaptureMouseDown(false);SetInputMode(Mode);
    }
    if(PlayerInput) PlayerInput->FlushPressedKeys();
    WheelRemainder=0;
    float UnusedX=0,UnusedY=0;GetInputMouseDelta(UnusedX,UnusedY);
}
void ABridgeNativePlayerController::ToggleInventory() {
    if(!IsValid(NativeReceiver) || !NativeReceiver->NativePlayActive || bPauseOpen || NativeReceiver->IsNativeSaving()) return;
    bInventoryOpen=!bInventoryOpen;
    if(!bInventoryOpen && NativeInventory) {NativeInventory->ReturnCursor();NativeInventory->SaveProfile();}
    StopNativeInput();SetMenuInput();
}
void ABridgeNativePlayerController::TogglePause() {
    if(!IsValid(NativeReceiver) || !NativeReceiver->NativePlayActive) return;
    if(bInventoryOpen) {ToggleInventory();return;}
    bPauseOpen=!bPauseOpen;
    StopNativeInput();
    if(bPauseOpen) {NativeReceiver->NativeSave();if(NativeInventory) NativeInventory->SaveProfile();}
    SetPause(bPauseOpen || NativeReceiver->IsNativeSaving());SetMenuInput();
}
void ABridgeNativePlayerController::SelectNativeHotbar(int32 Slot) {
    if(NativeInventory) NativeInventory->SelectHotbar(Slot);
    UpdateSelectedItem();
}
void ABridgeNativePlayerController::UpdateSelectedItem() {
    if(!NativeInventory || !IsValid(NativeReceiver)) return;
    const int32 Slot=NativeInventory->GetSelectedSlot();
    const FString Item=NativeInventory->GetSelectedItemId();
    if(Slot!=SelectedSlot || Item!=SelectedItem) {
        SelectedSlot=Slot;SelectedItem=Item;NativeReceiver->NativeSelect(Item);
    }
}
void ABridgeNativePlayerController::RouteMenuInput() {
    auto* Hud=Cast<ABridgeNativeHUD>(GetHUD());if(!Hud) return;
    float X=0,Y=0;
    if(GetMousePosition(X,Y)) {
        if(WasInputKeyJustPressed(EKeys::LeftMouseButton)) Hud->HandlePointer(EKeys::LeftMouseButton,FVector2D(X,Y));
        if(WasInputKeyJustPressed(EKeys::RightMouseButton)) Hud->HandlePointer(EKeys::RightMouseButton,FVector2D(X,Y));
    }
    const float Wheel=GetInputAnalogKeyState(EKeys::MouseWheelAxis);
    const int32 Steps=BridgeNativeInputMath::WheelSteps(Wheel,MouseWheelSensitivity,WheelRemainder);
    if(Steps) Hud->HandleScroll(Steps);
}
bool ABridgeNativePlayerController::InputKey(const FInputKeyEventArgs& Params) {
    const bool Result=Super::InputKey(Params);
    if(IsValid(NativeReceiver) && NativeReceiver->NativePlayActive && Params.Event==IE_Pressed && Params.Key==EKeys::F3) {
        if(auto* Hud=Cast<ABridgeNativeHUD>(GetHUD())) return Hud->HandleKey(Params.Key) || Result;
    }
    if((bInventoryOpen || bPauseOpen) && (Params.Event==IE_Pressed || Params.Event==IE_Repeat)) {
        if(auto* Hud=Cast<ABridgeNativeHUD>(GetHUD())) {
            if(Hud->HasSearchFocus()) return Result;
            if(Hud->HandleKey(Params.Key)) return true;
            const bool Shift=IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
            const uint32* KeyCode=nullptr;const uint32* CharacterCode=nullptr;
            FInputKeyManager::Get().GetCodesFromKey(Params.Key,KeyCode,CharacterCode);
            if(CharacterCode && *CharacterCode>=32 && *CharacterCode<127) {
                const TCHAR TypedCharacter=TCHAR(*CharacterCode);
                Hud->HandleText(FChar::IsAlpha(TypedCharacter) ? (Shift ? FChar::ToUpper(TypedCharacter) : FChar::ToLower(TypedCharacter)) : TypedCharacter);
            }
        }
    }
    return Result;
}
void ABridgeNativePlayerController::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    FindReceiver();
    if(!IsValid(NativeReceiver) || !NativeReceiver->NativePlayActive) {
        if(bWasNative) {if(bPauseOpen) SetPause(false);bInventoryOpen=bPauseOpen=false;bShowMouseCursor=false;bWasNative=false;}
        return;
    }
    if(!bWasNative) {
        bWasNative=true;SetMenuInput();
        if(auto* BridgePawn=Cast<ABridgeCharacter>(GetPawn())) {
            BridgePawn->ConfigureNativeViewOptions(BobView,NativeFovEffectScale);
            BridgePawn->SetMinecraftFov(NativeBaseFov);
        }
    }
    InitializeNativeInventory();
    if(WasInputKeyJustPressed(EKeys::Escape)) {TogglePause();return;}
    // Restore failures stop gameplay but retain Escape and pause-menu access.
    // Saving remains guarded by the receiver's valid-inventory check.
    if(bPauseOpen) {RouteMenuInput();StopNativeInput();return;}
    if(bSavedInventoryRejected) {NativeInputStatus=InventoryRestoreError;StopNativeInput();return;}
    if(NativeInventory && bInventoryInitialized) NativeInventory->TickAutosave(FPlatformTime::Seconds());
    const auto* NativeHud=Cast<ABridgeNativeHUD>(GetHUD());
    if(Pressed(TEXT("key.inventory")) && (!NativeHud || !NativeHud->HasSearchFocus())) {ToggleInventory();return;}
    if(bInventoryOpen || bPauseOpen) {RouteMenuInput();StopNativeInput();UpdateSelectedItem();return;}
    const bool HasFocus=Focused();
    if(!HasFocus) {StopNativeInput();bLastFocused=false;return;}
    if(!bLastFocused) {if(PlayerInput) PlayerInput->FlushPressedKeys();bLastFocused=true;}
    if(!NativeReceiver->IsNativeReady()) {NativeInputStatus=NativeReceiver->NativeStatus;StopNativeInput();return;}
    NativeInputStatus=TEXT("Direct UE input");
    auto* BridgePawn=Cast<ABridgeCharacter>(GetPawn());
    if(!BridgePawn) return;
    int32 Width=0,Height=0;GetViewportSize(Width,Height);
    if(Width>0 && Height>0) {BridgePawn->BridgeCamera->bConstrainAspectRatio=false;BridgePawn->BridgeCamera->AspectRatio=float(Width)/Height;}
    if(bFlightApplied && bFlying && !BridgePawn->BridgeFlying) bFlying=false;
    if(!NativeReceiver->NativeCreative) bFlying=false;
    const double Now=FPlatformTime::Seconds();
    if(Pressed(TEXT("key.jump"))) {
        if(NativeReceiver->NativeCreative && BridgeNativeInputMath::IsDoubleTap(PreviousJumpTap,Now)) {bFlying=!bFlying;PreviousJumpTap=-1;}
        else PreviousJumpTap=Now;
    }
    if(Pressed(TEXT("key.forward"))) {
        if(BridgeNativeInputMath::IsDoubleTap(PreviousForwardTap,Now)) bDoubleSprint=true;
        PreviousForwardTap=Now;
    }
    if(!Down(TEXT("key.forward")) || Down(TEXT("key.sneak"))) bDoubleSprint=false;
    if(ToggleCrouchOption && Pressed(TEXT("key.sneak"))) bToggleSneak=!bToggleSneak;
    if(ToggleSprintOption && Pressed(TEXT("key.sprint"))) bToggleSprint=!bToggleSprint;
    if(Pressed(TEXT("key.togglePerspective"))) NativePerspective=(NativePerspective+1)%3;
    if(Pressed(TEXT("key.toggleGui"))) NativeHudVisible=!NativeHudVisible;
    float MouseX=0,MouseY=0;GetInputMouseDelta(MouseX,MouseY);
    const float Degrees=float(BridgeNativeInputMath::MouseDegreesPerCount(MouseSensitivity));
    const FRotator PreviousRotation=GetControlRotation();
    // UE MouseY is positive upwards; Minecraft exposes independent axis inversions.
    const FRotator Rotation(float(BridgeNativeInputMath::ClampPitch(PreviousRotation.Pitch+MouseY*Degrees*(InvertMouse ? -1.f : 1.f))),
        FRotator::NormalizeAxis(PreviousRotation.Yaw+MouseX*Degrees*(InvertMouseX ? -1.f : 1.f)),0);
    SetControlRotation(Rotation);
    const float Forward=(Down(TEXT("key.forward")) ? 1.f : 0.f)-(Down(TEXT("key.back")) ? 1.f : 0.f);
    const float Right=(Down(TEXT("key.right")) ? 1.f : 0.f)-(Down(TEXT("key.left")) ? 1.f : 0.f);
    const bool Sneak=ToggleCrouchOption ? bToggleSneak : Down(TEXT("key.sneak"));
    const bool Sprint=bDoubleSprint || (ToggleSprintOption ? bToggleSprint : Down(TEXT("key.sprint")));
    NativeReceiver->SetNativeInput(Forward,Right,Down(TEXT("key.jump")),Sneak,Sprint,bFlying,NativePerspective,Rotation.Yaw,Rotation.Pitch);
    bFlightApplied=true;
    BridgePawn->ApplyNativePresentation(NativePerspective,LeftHanded,SkinLayers,SlimArms,DeltaSeconds);
    for(int32 I=0;I<9;++I) if(Pressed(FString::Printf(TEXT("key.hotbar.%d"),I+1))) SelectNativeHotbar(I);
    const float Wheel=GetInputAnalogKeyState(EKeys::MouseWheelAxis);
    const int32 WheelSteps=BridgeNativeInputMath::WheelSteps(Wheel,MouseWheelSensitivity,WheelRemainder);
    if(WheelSteps && NativeInventory) NativeInventory->ScrollHotbar(WheelSteps);
    UpdateSelectedItem();
    if(Down(TEXT("key.attack")) && (Pressed(TEXT("key.attack")) || Now>=NextAttack)) {NativeReceiver->NativeAction(TEXT("break"));NextAttack=Now+.25;}
    const bool UseDown=Down(TEXT("key.use"));
    const bool BowSelected=SelectedItem==TEXT("minecraft:bow");
    if(bBowHeld && (!UseDown || !BowSelected)) {
        NativeReceiver->NativeAction(BowSelected ? TEXT("use_release") : TEXT("use_cancel"));bBowHeld=false;
    }
    if(UseDown && BowSelected && !bBowHeld) {NativeReceiver->NativeAction(TEXT("use_start"));bBowHeld=true;}
    else if(UseDown && !BowSelected && (Pressed(TEXT("key.use")) || Now>=NextUse)) {NativeReceiver->NativeAction(TEXT("place"));NextUse=Now+.20;}
    if(Pressed(TEXT("key.pickItem"))) NativeReceiver->NativeAction(TEXT("pick"));
    if(Pressed(TEXT("key.swapOffhand"))) {
        NativeInputStatus=TEXT("Offhand swapping is not available in this native build");
        UE_LOG(LogTemp,Warning,TEXT("Bridge native input: offhand swap is not implemented; inventory was not changed"));
    }
    if(Pressed(TEXT("key.drop")) && NativeInventory) {
        const bool All=IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
        const auto Stack=NativeInventory->Selected();
        const int32 Count=All ? Stack.Count : 1;
        if(!Stack.ItemId.IsEmpty() && Stack.Count>0 && NativeReceiver->NativeDrop(Stack.ItemId,Count)) NativeInventory->ConsumeSelected(Count);
    }
}
