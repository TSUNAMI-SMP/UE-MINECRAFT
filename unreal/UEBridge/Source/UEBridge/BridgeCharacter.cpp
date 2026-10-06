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
    GetCharacterMovement()->MaxWalkSpeed=430;
    GetCharacterMovement()->MaxWalkSpeedCrouched=130;
    GetCharacterMovement()->JumpZVelocity=420;
    GetCharacterMovement()->MaxStepHeight=50;
    GetCharacterMovement()->SetCrouchedHalfHeight(75);
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch=true;
    GetCharacterMovement()->DefaultLandMovementMode = MOVE_None;
}
void ABridgeCharacter::ApplyMinecraftPose(double BodyHeight, double EyeHeight, bool Sneak) {
    const float HalfHeight = float(BodyHeight * 50);
    GetCapsuleComponent()->SetCapsuleSize(FMath::Min(30.f,HalfHeight),HalfHeight,false);
    BridgeCamera->SetRelativeLocation(FVector(0,0,EyeHeight*100-HalfHeight));
    BaseEyeHeight = float(EyeHeight*100); BridgeSneaking = Sneak; bIsCrouched = Sneak;
}

void ABridgeCharacter::SetAuthorityEnabled(bool Enabled) {
    if(UEAuthority==Enabled) return;
    UEAuthority=Enabled; PreviousJump=false; StopJumping();
    auto* Movement=GetCharacterMovement(); Movement->StopMovementImmediately();
    Movement->GravityScale=Enabled ? 1.f : 0.f;
    Movement->SetMovementMode(Enabled ? MOVE_Walking : MOVE_None);
}
void ABridgeCharacter::ApplyUEInput(float Forward,float Right,bool JumpHeld,bool Sneak) {
    if(!UEAuthority) return;
    if(Sneak) Crouch(); else UnCrouch();
    const FRotator Heading(0,GetControlRotation().Yaw,0);
    FVector Direction=Heading.Vector()*Forward + FRotationMatrix(Heading).GetUnitAxis(EAxis::Y)*Right;
    const float Magnitude=FMath::Min(1.f,Direction.Size());
    if(Magnitude>0) AddMovementInput(Direction.GetSafeNormal(),Magnitude);
    if(JumpHeld && !PreviousJump) Jump();
    if(!JumpHeld) StopJumping(); PreviousJump=JumpHeld;
}
void ABridgeCharacter::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if(UEAuthority) {
        BridgeSneaking=bIsCrouched;
        const float Eye=bIsCrouched ? 127.f : 162.f;
        BridgeCamera->SetRelativeLocation(FVector(0,0,Eye-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    }
}
