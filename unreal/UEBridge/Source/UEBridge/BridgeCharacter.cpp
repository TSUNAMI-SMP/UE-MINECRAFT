#include "BridgeCharacter.h"
#include "BridgeCharacterMath.h"
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
#include "BridgePlayerAppearance.h"
#include "ProceduralMeshComponent.h"

namespace {
FVector PosePosition(const BridgeCharacterMath::Pose& Pose) {return FVector(Pose.Position.X,Pose.Position.Y,Pose.Position.Z);}
FQuat PoseRotation(const BridgeCharacterMath::Pose& Pose) {return FQuat(Pose.Rotation.X,Pose.Rotation.Y,Pose.Rotation.Z,Pose.Rotation.W).GetNormalized();}
// Minecraft cuboid net: right/front/left/back across the middle, top/bottom above.
// Model scale is the vanilla 1/16 block = 6.25 cm; UVs stay on the 64x64 skin atlas.
void SkinCuboid(UProceduralMeshComponent* Part,float PixelWidth,float PixelHeight,float PixelDepth,
    float TextureU,float TextureV,float Inflate,bool AbovePivot=false,float PivotTop=0,float LateralCenter=0) {
    const float W=PixelWidth*6.25f+Inflate*2,H=PixelHeight*6.25f+Inflate*2,D=PixelDepth*6.25f+Inflate*2;
    const float Top=(AbovePivot ? PixelHeight*6.25f+Inflate : Inflate)+PivotTop;
    const float Bottom=Top-H;
    TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    auto Face=[&](FVector A,FVector B,FVector C,FVector E,FVector Normal,float U,float V,float UW,float VH) {
        const FVector Center(0,LateralCenter,0);
        const int32 First=Vertices.Num();Vertices.Append({A+Center,B+Center,C+Center,E+Center});
        UV.Append({FVector2D(U/64.f,V/64.f),FVector2D((U+UW)/64.f,V/64.f),FVector2D((U+UW)/64.f,(V+VH)/64.f),FVector2D(U/64.f,(V+VH)/64.f)});
        for(int32 I=0;I<4;++I) {Normals.Add(Normal);Colors.Add(FLinearColor::White);Tangents.Add(FProcMeshTangent((B-A).GetSafeNormal(),false));}
        if(FVector::DotProduct(FVector::CrossProduct(B-A,C-A),Normal)>0) Indices.Append({First,First+1,First+2,First,First+2,First+3});
        else Indices.Append({First,First+2,First+1,First,First+3,First+2});
    };
    // UE +X is the player's front, +Y their right, +Z up.
    Face(FVector(D/2,W/2,Top),FVector(D/2,-W/2,Top),FVector(D/2,-W/2,Bottom),FVector(D/2,W/2,Bottom),FVector(1,0,0),TextureU+PixelDepth,TextureV+PixelDepth,PixelWidth,PixelHeight);
    Face(FVector(-D/2,-W/2,Top),FVector(-D/2,W/2,Top),FVector(-D/2,W/2,Bottom),FVector(-D/2,-W/2,Bottom),FVector(-1,0,0),TextureU+PixelDepth*2+PixelWidth,TextureV+PixelDepth,PixelWidth,PixelHeight);
    Face(FVector(-D/2,W/2,Top),FVector(D/2,W/2,Top),FVector(D/2,W/2,Bottom),FVector(-D/2,W/2,Bottom),FVector(0,1,0),TextureU,TextureV+PixelDepth,PixelDepth,PixelHeight);
    Face(FVector(D/2,-W/2,Top),FVector(-D/2,-W/2,Top),FVector(-D/2,-W/2,Bottom),FVector(D/2,-W/2,Bottom),FVector(0,-1,0),TextureU+PixelDepth+PixelWidth,TextureV+PixelDepth,PixelDepth,PixelHeight);
    Face(FVector(-D/2,W/2,Top),FVector(-D/2,-W/2,Top),FVector(D/2,-W/2,Top),FVector(D/2,W/2,Top),FVector(0,0,1),TextureU+PixelDepth,TextureV,PixelWidth,PixelDepth);
    Face(FVector(D/2,W/2,Bottom),FVector(D/2,-W/2,Bottom),FVector(-D/2,-W/2,Bottom),FVector(-D/2,W/2,Bottom),FVector(0,0,-1),TextureU+PixelDepth+PixelWidth,TextureV,PixelWidth,PixelDepth);
    Part->ClearAllMeshSections();
    Part->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
}
}

ABridgeCharacter::ABridgeCharacter() {
    GetCapsuleComponent()->InitCapsuleSize(30.f, 90.f);
    BridgeCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("BridgeCamera"));
    BridgeCamera->SetupAttachment(GetCapsuleComponent());
    BridgeCamera->SetRelativeLocation(FVector(0, 0, 72)); // MC eye = 162 cm above feet.
    BridgeCamera->bUsePawnControlRotation = true;
    SetMinecraftFov(80.f);
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
    BaseEyeHeight=72.f;CrouchedEyeHeight=52.f;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto Prepare=[&](UStaticMeshComponent* VisualMesh,USceneComponent* Parent) {
        VisualMesh->SetupAttachment(Parent);if(Cube.Succeeded()) VisualMesh->SetStaticMesh(Cube.Object);
        VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);VisualMesh->SetCastShadow(false);
        VisualMesh->SetCanEverAffectNavigation(false);VisualMesh->SetGenerateOverlapEvents(false);
    };
    Sleeve=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonSleeve"));Prepare(Sleeve,BridgeCamera);
    Hand=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonHand"));Prepare(Hand,BridgeCamera);
    HeldMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonItem"));Prepare(HeldMesh,BridgeCamera);
    Sleeve->SetRelativeScale3D(FVector(.28,.10,.10));Hand->SetRelativeScale3D(FVector(.12,.10,.10));
    Sleeve->SetRelativeRotation(FRotator(-35,-15,0));Hand->SetRelativeRotation(FRotator(-35,-15,0));
    Sleeve->SetRelativeLocation(FVector(22,19,-23));Hand->SetRelativeLocation(FVector(37,15,-14));
    AvatarRoot=CreateDefaultSubobject<USceneComponent>(TEXT("MinecraftAvatar"));AvatarRoot->SetupAttachment(GetCapsuleComponent());
    auto PrepareSkin=[](UProceduralMeshComponent* Part,USceneComponent* Parent) {
        Part->SetupAttachment(Parent);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);Part->SetCanEverAffectNavigation(false);
    };
    for(int32 I=0;I<6;++I) {
        auto* Part=CreateDefaultSubobject<UProceduralMeshComponent>(*FString::Printf(TEXT("SkinPart%d"),I));
        PrepareSkin(Part,AvatarRoot);AvatarParts.Add(Part);
        auto* Layer=CreateDefaultSubobject<UProceduralMeshComponent>(*FString::Printf(TEXT("SkinLayer%d"),I));
        PrepareSkin(Layer,Part);AvatarLayers.Add(Layer);
    }
    SkinArm=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MinecraftFirstPersonArm"));PrepareSkin(SkinArm,BridgeCamera);SkinArm->SetCastShadow(false);
    SkinSleeve=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MinecraftFirstPersonSleeve"));PrepareSkin(SkinSleeve,SkinArm);SkinSleeve->SetCastShadow(false);
    AvatarRoot->SetVisibility(false,true);SkinArm->SetVisibility(false,true);
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
    BaseEyeHeight = float(EyeHeight*100)-HalfHeight; BridgeSneaking = Sneak; bIsCrouched = Sneak;
    RemoteEyeHeight=float(EyeHeight*100);
}

void ABridgeCharacter::SetAuthorityEnabled(bool Enabled) {
    if(UEAuthority==Enabled) return;
    UEAuthority=Enabled; PreviousJump=false; SprintRequested=false;BodyYawInitialized=false;StopJumping();
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
    SprintRequested=Sprint && !Sneak && Forward>0;
    GetCharacterMovement()->MaxWalkSpeed=SprintRequested ? 561.2f : 431.7f;
    const FRotator Heading(0,GetControlRotation().Yaw,0);
    FVector Direction=Heading.Vector()*Forward + FRotationMatrix(Heading).GetUnitAxis(EAxis::Y)*Right;
    const float Magnitude=FMath::Min(1.f,Direction.Size());
    if(Magnitude>0) AddMovementInput(Direction.GetSafeNormal(),Magnitude);
    if(JumpHeld && (!PreviousJump || GetCharacterMovement()->IsMovingOnGround())) Jump();
    if(!JumpHeld) StopJumping(); PreviousJump=JumpHeld;
}
bool ABridgeCharacter::CanJumpInternal_Implementation() const {
    // ACharacter rejects bIsCrouched and CanAttemptJump rejects bWantsToCrouch.
    // MC permits a ground jump while retaining the shorter capsule. Remove only
    // those crouch gates by checking IsJumpAllowed and the ground mode directly:
    // movement still owns jump count, plane constraints and all ceiling sweeps.
    const auto* Movement=GetCharacterMovement();
    if(UEAuthority && bIsCrouched && JumpMaxCount==1 && GetJumpMaxHoldTime()<=0.f)
        return Movement && Movement->IsJumpAllowed() && Movement->IsMovingOnGround() && JumpCurrentCount<JumpMaxCount;
    return Super::CanJumpInternal_Implementation();
}
bool ABridgeCharacter::IsAuthoritySprinting() const {
    const FVector Forward=FRotator(0,GetControlRotation().Yaw,0).Vector();
    return UEAuthority && SprintRequested && !bIsCrouched && FVector::DotProduct(GetVelocity(),Forward)>5.f;
}
float ABridgeCharacter::GetFloorGapCm() const {
    const auto* Movement=GetCharacterMovement();
    if(!UEAuthority || !Movement || !Movement->IsMovingOnGround() || !Movement->CurrentFloor.IsWalkableFloor()) return 0.f;
    // CharacterMovement's 1.9..2.4 cm floor suspension is collision clearance,
    // not Minecraft eye height. Do not resize/move the physical capsule.
    return FMath::Clamp(Movement->CurrentFloor.GetDistanceToFloor(),0.f,3.f);
}
FVector ABridgeCharacter::GetMinecraftFeetPosition() const {
    return GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+GetFloorGapCm());
}
float ABridgeCharacter::GetHandSwing() const {
    return PlayerSwing>0.f ? PlayerSwing : (SwingRemaining>0.f ? 1.f-SwingRemaining/float(BridgeCharacterMath::SwingSeconds) : 0.f);
}
void ABridgeCharacter::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if(UEAuthority) {
        BridgeSneaking=bIsCrouched;
    }
    BridgeEyeHeightCm=UEAuthority ? float(bIsCrouched ? BridgeCharacterMath::CrouchedEyeCm : BridgeCharacterMath::StandingEyeCm) : RemoteEyeHeight;
    BridgeFloorGapCm=GetFloorGapCm();BaseEyeHeight=BridgeEyeHeightCm-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-BridgeFloorGapCm;
    BridgeSprinting=IsAuthoritySprinting();
    FovSprintMultiplier=float(BridgeCharacterMath::SprintFovMultiplier(FovSprintMultiplier,BridgeSprinting,DeltaSeconds));
    BridgeVerticalFov=MinecraftBaseFov*FovSprintMultiplier;
    BridgeCamera->SetFieldOfView(float(BridgeCharacterMath::HorizontalFov(BridgeVerticalFov,BridgeCamera->AspectRatio)));
    SwingRemaining=FMath::Max(0.f,SwingRemaining-DeltaSeconds);
    const float ViewYaw=GetControlRotation().Yaw;
    if(!BodyYawInitialized) {BridgeBodyYaw=ViewYaw;BodyYawInitialized=true;}
    BridgeBodyYaw=float(BridgeCharacterMath::BodyYaw(BridgeBodyYaw,ViewYaw,GetVelocity().Rotation().Yaw,
        GetVelocity().Size2D(),GetHandSwing()>0.f,PlayerUsingItem && PlayerUseAction==TEXT("block"),DeltaSeconds));
    LimbAmplitude=float(BridgeCharacterMath::Smooth(LimbAmplitude,FMath::Min(1.f,GetVelocity().Size2D()/500.f),8.f,DeltaSeconds));
    const bool Backwards=FVector::DotProduct(GetVelocity(),FRotator(0,BridgeBodyYaw,0).Vector())<0.f;
    BobPhase+=DeltaSeconds*20.f*.6662f*LimbAmplitude*(Backwards ? -1.f : 1.f);
    const float Bob=UEAuthority && GetCharacterMovement()->IsMovingOnGround() ? FMath::Sin(BobPhase)*FMath::Min(1.f,GetVelocity().Size2D()/432.f) : 0;
    UpdatePlayerCamera();UpdateAvatar(Bob);
    FIntVector Block;FVector Normal;
    FVector EyePosition;FRotator AimRotation;GetEyeAim(EyePosition,AimRotation);
    const bool Aimed=UEAuthority && IsValid(InteractionWorld) && InteractionWorld->Aim(EyePosition,AimRotation,500.f,Block,Normal,this);
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
        auto Tint=[&](UStaticMeshComponent* VisualMesh,const FColor& ColorValue) {
            UMaterialInterface* Base=Material ? Material : VisualMesh->GetMaterial(0);if(!Base) return;
            auto* Dynamic=UMaterialInstanceDynamic::Create(Base,this);
            Dynamic->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(ColorValue));
            VisualMesh->SetMaterial(0,Dynamic);
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
    }
    VisualsConfigured=true;VisualMaterial=Material;VisualPalette=Palette;VisualItem=Item;VisualBlock=Block;VisualColor=Color;
}

void ABridgeCharacter::SetInteractionWorld(ABridgeWorld* Imported) { InteractionWorld=Imported; }

void ABridgeCharacter::ConfigureAppearance(UBridgePlayerAppearance* Appearance) {
    if(PlayerAppearance==Appearance && AvatarGeometryReady) return;
    PlayerAppearance=Appearance;AvatarGeometryReady=false;
    if(Appearance && !HasPlayerVisuals) PlayerSlim=Appearance->IsSlim;
    BuildAvatarGeometry();
}

void ABridgeCharacter::ApplyPlayerVisuals(int32 Perspective,float SwingProgress,float EquipProgress,bool UsingItem,const FString& UseAction,float UseProgress,bool LeftHanded,int32 SkinLayers,bool SlimArms) {
    CameraPerspective=FMath::Clamp(Perspective,0,2);
    PlayerSwing=FMath::Clamp(SwingProgress,0.f,1.f);PlayerEquip=FMath::Clamp(EquipProgress,0.f,1.f);
    PlayerUsingItem=UsingItem;PlayerUseAction=UseAction;PlayerUseProgress=FMath::Clamp(UseProgress,0.f,1.f);
    PlayerSkinLayers=SkinLayers&127;
    const bool Changed=PlayerLeftHanded!=LeftHanded || PlayerSlim!=SlimArms;
    PlayerLeftHanded=LeftHanded;PlayerSlim=SlimArms;HasPlayerVisuals=true;
    if(Changed) AvatarGeometryReady=false;
    UpdatePlayerCamera();
}

void ABridgeCharacter::GetEyeAim(FVector& EyePosition,FRotator& AimRotation) const {
    const float EyeHeight=UEAuthority ? float(bIsCrouched ? BridgeCharacterMath::CrouchedEyeCm : BridgeCharacterMath::StandingEyeCm) : RemoteEyeHeight;
    EyePosition=GetMinecraftFeetPosition()+FVector(0,0,EyeHeight);
    AimRotation=GetControlRotation();
}

void ABridgeCharacter::SetMinecraftFov(float VerticalFov) {
    BridgeCamera->AspectRatio=16.f/9.f;
    MinecraftBaseFov=FMath::IsFinite(VerticalFov) ? FMath::Clamp(VerticalFov,30.f,110.f) : 80.f;
    BridgeVerticalFov=MinecraftBaseFov*FovSprintMultiplier;
    BridgeCamera->SetFieldOfView(float(BridgeCharacterMath::HorizontalFov(BridgeVerticalFov,BridgeCamera->AspectRatio)));
}

void ABridgeCharacter::UpdatePlayerCamera() {
    FVector EyePosition;FRotator AimRotation;GetEyeAim(EyePosition,AimRotation);
    BridgeCamera->bUsePawnControlRotation=CameraPerspective==0;
    if(CameraPerspective==0) {BridgeCamera->SetWorldLocationAndRotation(EyePosition,AimRotation);return;}
    const FVector Desired=EyePosition+AimRotation.Vector()*(CameraPerspective==1 ? -400.f : 400.f);
    FVector CameraPosition=Desired;
    if(GetWorld()) {
        FHitResult Obstruction;FCollisionQueryParams Params(SCENE_QUERY_STAT(MinecraftThirdPerson),false,this);
        if(GetWorld()->SweepSingleByChannel(Obstruction,EyePosition,Desired,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12.f),Params))
            CameraPosition=EyePosition+(Desired-EyePosition).GetSafeNormal()*FMath::Max(0.f,Obstruction.Distance-2.f);
    }
    const FRotator CameraRotation=CameraPerspective==1 ? AimRotation : FRotator(-AimRotation.Pitch,AimRotation.Yaw+180.f,0);
    BridgeCamera->SetWorldLocationAndRotation(CameraPosition,CameraRotation);
}

void ABridgeCharacter::BuildAvatarGeometry() {
    const float ArmWidth=PlayerSlim ? 3.f : 4.f;
    const float Widths[]={8,8,ArmWidth,ArmWidth,4,4};
    const float Heights[]={8,12,12,12,12,12};
    const float Depths[]={8,4,4,4,4,4};
    const FVector2D BaseUV[]={FVector2D(0,0),FVector2D(16,16),FVector2D(40,16),FVector2D(32,48),FVector2D(0,16),FVector2D(16,48)};
    const FVector2D LayerUV[]={FVector2D(32,0),FVector2D(16,32),FVector2D(40,32),FVector2D(48,48),FVector2D(0,32),FVector2D(0,48)};
    const FVector Positions[]={FVector(0,0,150),FVector(0,0,150),FVector(0,31.25,137.5),FVector(0,-31.25,137.5),FVector(0,12.5,75),FVector(0,-12.5,75)};
    for(int32 I=0;I<6;++I) {
        const bool ArmPart=I==2 || I==3;
        const float ArmSide=I==2 ? 1.f : -1.f;
        const float Center=ArmPart ? ArmSide*(PlayerSlim ? 3.125f : 6.25f) : 0.f;
        const float PivotTop=ArmPart ? 12.5f : 0.f;
        SkinCuboid(AvatarParts[I],Widths[I],Heights[I],Depths[I],BaseUV[I].X,BaseUV[I].Y,0,I==0,PivotTop,Center);
        SkinCuboid(AvatarLayers[I],Widths[I],Heights[I],Depths[I],LayerUV[I].X,LayerUV[I].Y,I==0 ? 3.125f : 1.5625f,I==0,PivotTop,Center);
        AvatarParts[I]->SetRelativeLocation(Positions[I]);
        if(IsValid(PlayerAppearance) && PlayerAppearance->SkinMaterial) {
            AvatarParts[I]->SetMaterial(0,PlayerAppearance->SkinMaterial);AvatarLayers[I]->SetMaterial(0,PlayerAppearance->SkinMaterial);
        }
    }
    const int32 ArmIndex=PlayerLeftHanded ? 3 : 2;
    SkinCuboid(SkinArm,ArmWidth,12,4,BaseUV[ArmIndex].X,BaseUV[ArmIndex].Y,0);
    SkinCuboid(SkinSleeve,ArmWidth,12,4,LayerUV[ArmIndex].X,LayerUV[ArmIndex].Y,1.5625f);
    if(IsValid(PlayerAppearance) && PlayerAppearance->SkinMaterial) {
        SkinArm->SetMaterial(0,PlayerAppearance->SkinMaterial);SkinSleeve->SetMaterial(0,PlayerAppearance->SkinMaterial);
    }
    AvatarGeometryReady=true;
}

void ABridgeCharacter::UpdateAvatar(float Bob) {
    if(!AvatarGeometryReady) BuildAvatarGeometry();
    const bool HasSkin=IsValid(PlayerAppearance) && IsValid(PlayerAppearance->SkinMaterial);
    const bool FirstPerson=CameraPerspective==0 && (UEAuthority || HasPlayerVisuals);
    const bool ThirdPerson=CameraPerspective!=0 && (UEAuthority || HasPlayerVisuals) && HasSkin;
    AvatarRoot->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-GetFloorGapCm()+.09375f));
    AvatarRoot->SetRelativeRotation(FRotator(0,FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,BridgeBodyYaw),0));
    AvatarRoot->SetRelativeScale3D(FVector(.9375f)); // PlayerEntityRenderer.scale in vanilla.
    AvatarRoot->SetVisibility(ThirdPerson,true);
    const int32 LayerMasks[]={64,2,8,4,32,16};
    for(int32 I=0;I<6;++I) AvatarLayers[I]->SetVisibility(ThirdPerson && (PlayerSkinLayers&LayerMasks[I])!=0);
    // Vanilla sneaking shifts the head/body/shoulders down and hips back; capsule collision
    // remains entirely controlled by CharacterMovement, not these presentation transforms.
    AvatarParts[0]->SetRelativeLocation(FVector(0,0,bIsCrouched ? 123.75f : 150.f));
    AvatarParts[1]->SetRelativeLocation(FVector(0,0,bIsCrouched ? 130.f : 150.f));
    AvatarParts[2]->SetRelativeLocation(FVector(0,31.25,bIsCrouched ? 105.f : 137.5f));
    AvatarParts[3]->SetRelativeLocation(FVector(0,-31.25,bIsCrouched ? 105.f : 137.5f));
    AvatarParts[4]->SetRelativeLocation(FVector(bIsCrouched ? -25.f : 0,12.5,bIsCrouched ? 73.75f : 75.f));
    AvatarParts[5]->SetRelativeLocation(FVector(bIsCrouched ? -25.f : 0,-12.5,bIsCrouched ? 73.75f : 75.f));
    const float Gait=FMath::Cos(BobPhase)*LimbAmplitude*FMath::RadiansToDegrees(1.f);
    AvatarParts[0]->SetRelativeRotation(FRotator(GetControlRotation().Pitch,FMath::FindDeltaAngleDegrees(BridgeBodyYaw,GetControlRotation().Yaw),0));
    AvatarParts[1]->SetRelativeRotation(FRotator(bIsCrouched ? -25.f : 0,0,0));
    AvatarParts[2]->SetRelativeRotation(FRotator(Gait+(bIsCrouched ? -23.f : 0),0,0));AvatarParts[3]->SetRelativeRotation(FRotator(-Gait+(bIsCrouched ? -23.f : 0),0,0));
    AvatarParts[4]->SetRelativeRotation(FRotator(-Gait*1.4f,0,0));AvatarParts[5]->SetRelativeRotation(FRotator(Gait*1.4f,0,0));
    const float NativeSwing=GetHandSwing();
    const float Side=PlayerLeftHanded ? -1.f : 1.f;
    const auto ArmPose=BridgeCharacterMath::FirstPersonArm(NativeSwing,PlayerEquip,PlayerLeftHanded,PlayerSlim);
    const auto ItemPose=BridgeCharacterMath::FirstPersonBlock(NativeSwing,PlayerEquip,PlayerLeftHanded);
    FVector ArmPosition=PosePosition(ArmPose)+FVector(0,0,Bob);
    FQuat ArmRotation=PoseRotation(ArmPose);
    FVector ItemPosition=PosePosition(ItemPose)+FVector(0,0,Bob);
    FRotator ItemRotation=PoseRotation(ItemPose).Rotator();
    if(PlayerUsingItem) {
        if(PlayerUseAction==TEXT("eat") || PlayerUseAction==TEXT("drink")) {
            const float Shake=FMath::Sin(PlayerUseProgress*PI*24.f)*2.f;
            ArmPosition+=FVector(-6,-Side*17,15+Shake);ItemPosition+=FVector(-10,-Side*18,18+Shake);ItemRotation.Pitch-=35;
        } else if(PlayerUseAction==TEXT("bow") || PlayerUseAction==TEXT("crossbow")) {
            ArmPosition+=FVector(6,-Side*15,7);ItemPosition+=FVector(5+PlayerUseProgress*4,-Side*18,7);ItemRotation=FRotator(-10,Side*65,0);
        } else if(PlayerUseAction==TEXT("block")) {
            ArmPosition+=FVector(-6,-Side*17,6);ItemPosition+=FVector(-10,-Side*18,6);ItemRotation=FRotator(-35,Side*75,Side*20);
        }
    }
    // Vanilla's basic held-item path renders the item model, while its empty-hand
    // path renders the skin arm. Keep the two transforms/visibility paths separate.
    const bool EmptyHand=VisualItem.IsEmpty();
    SkinArm->SetVisibility(FirstPerson && HasSkin && EmptyHand);SkinSleeve->SetVisibility(FirstPerson && HasSkin && EmptyHand && (PlayerSkinLayers&(PlayerLeftHanded ? 4 : 8))!=0);
    SkinArm->SetRelativeLocationAndRotation(ArmPosition,ArmRotation);
    Sleeve->SetVisibility(FirstPerson && !HasSkin && EmptyHand);Hand->SetVisibility(FirstPerson && !HasSkin && EmptyHand);
    const float ArmWidthCm=PlayerSlim ? 18.75f : 25.f;
    Sleeve->SetRelativeScale3D(FVector(.25f,ArmWidthCm/100.f,.45f));Hand->SetRelativeScale3D(FVector(.25f,ArmWidthCm/100.f,.30f));
    Sleeve->SetRelativeLocationAndRotation(ArmPosition+ArmRotation.RotateVector(FVector(0,0,-22.5f)),ArmRotation);
    Hand->SetRelativeLocationAndRotation(ArmPosition+ArmRotation.RotateVector(FVector(0,0,-60.f)),ArmRotation);
    HeldMesh->SetVisibility((FirstPerson || ThirdPerson) && !VisualItem.IsEmpty());
    if(ThirdPerson) {
        const int32 ArmIndex=PlayerLeftHanded ? 3 : 2;
        HeldMesh->AttachToComponent(AvatarParts[ArmIndex],FAttachmentTransformRules::KeepRelativeTransform);
        HeldMesh->SetRelativeScale3D(VisualBlock.IsEmpty() ? FVector(.025,.035,.20) : FVector(BridgeCharacterMath::ThirdPersonBlockScale));
        const auto GripPose=BridgeCharacterMath::ThirdPersonBlock(PlayerLeftHanded);
        HeldMesh->SetRelativeLocationAndRotation(PosePosition(GripPose),PoseRotation(GripPose));
        if(NativeSwing>0) AvatarParts[ArmIndex]->AddLocalRotation(FRotator(float(BridgeCharacterMath::AttackPitch(NativeSwing,GetControlRotation().Pitch)),0,0));
    } else {
        HeldMesh->AttachToComponent(BridgeCamera,FAttachmentTransformRules::KeepRelativeTransform);
        HeldMesh->SetRelativeScale3D(VisualBlock.IsEmpty() ? FVector(.025,.035,.20) : FVector(BridgeCharacterMath::FirstPersonBlockScale));
        HeldMesh->SetRelativeLocationAndRotation(ItemPosition,ItemRotation);
    }
}
