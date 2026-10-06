#include "BridgeCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "BridgeBlockPalette.h"
#include "BridgeWorld.h"

ABridgeCharacter::ABridgeCharacter() {
    GetCapsuleComponent()->InitCapsuleSize(30.f, 90.f);
    BridgeCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("BridgeCamera"));
    BridgeCamera->SetupAttachment(GetCapsuleComponent());
    BridgeCamera->SetRelativeLocation(FVector(0, 0, 72)); // MC eye = 162 cm above feet.
    BridgeCamera->bUsePawnControlRotation = true;
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->GravityScale = 0;
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
    GetCharacterMovement()->MaxWalkSpeed=431.7f;
    GetCharacterMovement()->MaxAcceleration=6500.f;
    GetCharacterMovement()->BrakingDecelerationWalking=6500.f;
    GetCharacterMovement()->GroundFriction=8.f;
    GetCharacterMovement()->BrakingFrictionFactor=1.f;
    GetCharacterMovement()->AirControl=.35f;
    GetCharacterMovement()->FallingLateralFriction=.5f;
    GetCharacterMovement()->BrakingDecelerationFalling=0;
    JumpMaxHoldTime=0;
    GetCharacterMovement()->MaxWalkSpeedCrouched=130;
    GetCharacterMovement()->JumpZVelocity=900;
    GetCharacterMovement()->MaxStepHeight=50;
    GetCharacterMovement()->SetCrouchedHalfHeight(75);
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch=true;
    GetCharacterMovement()->DefaultLandMovementMode = MOVE_None;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto Prepare=[&](UStaticMeshComponent* Mesh,USceneComponent* Parent) {
        Mesh->SetupAttachment(Parent);if(Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);
        Mesh->SetCanEverAffectNavigation(false);Mesh->SetGenerateOverlapEvents(false);
    };
    Sleeve=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonSleeve"));Prepare(Sleeve,BridgeCamera);
    Hand=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonHand"));Prepare(Hand,BridgeCamera);
    HeldMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonItem"));Prepare(HeldMesh,BridgeCamera);
    Sleeve->SetRelativeScale3D(FVector(.28,.10,.10));Hand->SetRelativeScale3D(FVector(.12,.10,.10));
    Sleeve->SetRelativeRotation(FRotator(-35,-15,0));Hand->SetRelativeRotation(FRotator(-35,-15,0));
    Sleeve->SetRelativeLocation(FVector(22,19,-23));Hand->SetRelativeLocation(FVector(37,15,-14));
    AimRoot=CreateDefaultSubobject<USceneComponent>(TEXT("AimOutline"));AimRoot->SetupAttachment(GetCapsuleComponent());
    for(int32 I=0;I<12;++I) {
        auto* Edge=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("AimEdge%d"),I));
        Prepare(Edge,AimRoot);AimEdges.Add(Edge);
    }
    AimRoot->SetVisibility(false,true);HeldMesh->SetVisibility(false);
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
    // Landing uses DefaultLandMovementMode, not just the current movement mode.
    Movement->DefaultLandMovementMode=Enabled ? MOVE_Walking : MOVE_None;
    Movement->JumpZVelocity=900.f;
    const float WorldGravity=GetWorld() ? FMath::Abs(GetWorld()->GetGravityZ()) : 980.f;
    Movement->GravityScale=Enabled ? 3200.f/FMath::Max(1.f,WorldGravity) : 0.f;
    Movement->SetMovementMode(Enabled ? MOVE_Walking : MOVE_None);
}
void ABridgeCharacter::ApplyUEInput(float Forward,float Right,bool JumpHeld,bool Sneak,bool Sprint) {
    if(!UEAuthority) return;
    if(Sneak) Crouch(); else UnCrouch();
    GetCharacterMovement()->MaxWalkSpeed=Sprint && !Sneak && Forward>0 ? 561.2f : 431.7f;
    const FRotator Heading(0,GetControlRotation().Yaw,0);
    FVector Direction=Heading.Vector()*Forward + FRotationMatrix(Heading).GetUnitAxis(EAxis::Y)*Right;
    const float Magnitude=FMath::Min(1.f,Direction.Size());
    if(Magnitude>0) AddMovementInput(Direction.GetSafeNormal(),Magnitude);
    if(JumpHeld && (!PreviousJump || GetCharacterMovement()->IsMovingOnGround())) Jump();
    if(!JumpHeld) StopJumping(); PreviousJump=JumpHeld;
}
void ABridgeCharacter::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if(UEAuthority) {
        BridgeSneaking=bIsCrouched;
        const float Eye=bIsCrouched ? 127.f : 162.f;
        BridgeCamera->SetRelativeLocation(FVector(0,0,Eye-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    }
    Sleeve->SetVisibility(UEAuthority);Hand->SetVisibility(UEAuthority);
    HeldMesh->SetVisibility(UEAuthority && !VisualItem.IsEmpty());
    BobPhase+=DeltaSeconds*FMath::Min(GetVelocity().Size2D()/50.f,12.f);
    const float Bob=UEAuthority && GetCharacterMovement()->IsMovingOnGround() ? FMath::Sin(BobPhase)*FMath::Min(1.f,GetVelocity().Size2D()/432.f) : 0;
    SwingRemaining=FMath::Max(0.f,SwingRemaining-DeltaSeconds);
    const float Swing=SwingRemaining>0 ? FMath::Sin(SwingRemaining/.22f*PI)*8.f : 0;
    Sleeve->SetRelativeLocation(FVector(22+Swing,19,-23+Bob));
    Hand->SetRelativeLocation(FVector(37+Swing,15,-14+Bob));
    HeldMesh->SetRelativeLocation(FVector(44+Swing,15,-9+Bob));
    FIntVector Block;FVector Normal;
    FMinimalViewInfo View;BridgeCamera->GetCameraView(DeltaSeconds,View);
    const bool Aimed=UEAuthority && IsValid(InteractionWorld) && InteractionWorld->Aim(View.Location,View.Rotation,500.f,Block,Normal,this);
    AimRoot->SetVisibility(Aimed,true);
    if(Aimed) {
        AimRoot->SetWorldLocationAndRotation(InteractionWorld->BlockCenter(Block),FRotator::ZeroRotator);
        int32 Index=0;
        for(int32 Axis=0;Axis<3;++Axis) for(int32 A:{-1,1}) for(int32 B:{-1,1}) {
            FVector Offset=FVector::ZeroVector;Offset[(Axis+1)%3]=A*50.3;Offset[(Axis+2)%3]=B*50.3;
            FVector Scale(.006);Scale[Axis]=1.012;
            AimEdges[Index]->SetRelativeLocation(Offset);AimEdges[Index]->SetRelativeScale3D(Scale);++Index;
        }
    }
}

void ABridgeCharacter::ConfigureVisuals(UMaterialInterface* Material,UBridgeBlockPalette* Palette,const FString& Item,const FString& Block,int32 Color) {
    if(!VisualsConfigured || VisualMaterial!=Material) {
        auto Tint=[&](UStaticMeshComponent* Mesh,const FColor& ColorValue) {
            UMaterialInterface* Base=Material ? Material : Mesh->GetMaterial(0);if(!Base) return;
            auto* Dynamic=UMaterialInstanceDynamic::Create(Base,this);
            Dynamic->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(ColorValue));
            Mesh->SetMaterial(0,Dynamic);
        };
        Tint(Sleeve,FColor(45,100,165));Tint(Hand,FColor(199,150,113));
        for(auto& Edge:AimEdges) Tint(Edge,FColor(12,12,12));
    }
    if(!VisualsConfigured || VisualMaterial!=Material || VisualPalette!=Palette || VisualItem!=Item || VisualBlock!=Block || VisualColor!=Color) {
        UMaterialInterface* ItemMaterial=Palette && !Block.IsEmpty() ? Palette->Find(Block) : nullptr;
        if(!ItemMaterial) ItemMaterial=Material ? Material : Hand->GetMaterial(0);
        if(ItemMaterial) {
            auto* Dynamic=UMaterialInstanceDynamic::Create(ItemMaterial,this);
            const FColor ItemColor(Block.IsEmpty() ? 200 : ((Color>>16)&255),Block.IsEmpty() ? 180 : ((Color>>8)&255),Block.IsEmpty() ? 110 : (Color&255));
            Dynamic->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(ItemColor));HeldMesh->SetMaterial(0,Dynamic);
        }
        HeldMesh->SetRelativeScale3D(Block.IsEmpty() ? FVector(.03,.04,.22) : FVector(.18));
        HeldMesh->SetRelativeRotation(FRotator(0,25,-15));
    }
    VisualsConfigured=true;VisualMaterial=Material;VisualPalette=Palette;VisualItem=Item;VisualBlock=Block;VisualColor=Color;
}

void ABridgeCharacter::SetInteractionWorld(ABridgeWorld* Imported) { InteractionWorld=Imported; }
