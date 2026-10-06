#include "BridgeCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ABridgeCharacter::ABridgeCharacter() {
    GetCapsuleComponent()->InitCapsuleSize(30.f, 90.f);
    BridgeCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("BridgeCamera"));
    BridgeCamera->SetupAttachment(GetCapsuleComponent());
    BridgeCamera->SetRelativeLocation(FVector(0, 0, 72)); // MC eye = 162 cm above feet.
    BridgeCamera->bUsePawnControlRotation = true;
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->GravityScale = 0;
    GetCharacterMovement()->DefaultLandMovementMode = MOVE_None;
}
void ABridgeCharacter::ApplyMinecraftPose(double BodyHeight, double EyeHeight, bool Sneak) {
    const float HalfHeight = float(BodyHeight * 50);
    GetCapsuleComponent()->SetCapsuleSize(FMath::Min(30.f,HalfHeight),HalfHeight,false);
    BridgeCamera->SetRelativeLocation(FVector(0,0,EyeHeight*100-HalfHeight));
    BaseEyeHeight = float(EyeHeight*100); BridgeSneaking = Sneak; bIsCrouched = Sneak;
}
