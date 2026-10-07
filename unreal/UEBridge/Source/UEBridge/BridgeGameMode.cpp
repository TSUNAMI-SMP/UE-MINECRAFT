#include "BridgeGameMode.h"
#include "BridgeCharacter.h"
#include "BridgeNativePlayerController.h"
#include "BridgeNativeHUD.h"
ABridgeGameMode::ABridgeGameMode() {
    DefaultPawnClass=ABridgeCharacter::StaticClass();
    PlayerControllerClass=ABridgeNativePlayerController::StaticClass();
    HUDClass=ABridgeNativeHUD::StaticClass();
}
