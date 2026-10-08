#include "BridgeCharacter.h"
#include "BridgeCharacterMath.h"
#include "BridgeOutlineMath.h"
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
#include "BridgeBlockGeometry.h"
#include "BridgeWorld.h"
#include "BridgePlayerAppearance.h"
#include "ProceduralMeshComponent.h"

namespace {
UMaterialInstanceDynamic* CreateVisualInstance(UMaterialInterface* SourceMaterial,UObject* Outer) {
    if(!IsValid(SourceMaterial)) return nullptr;
    auto* SourceDynamic=Cast<UMaterialInstanceDynamic>(SourceMaterial);
    UMaterialInterface* ParentMaterial=SourceMaterial;
    // UE permits a material or constant instance as a MID parent, but never another MID.
    // HeldMesh already has a MID when its material is copied onto the procedural hand model.
    while(auto* DynamicParent=Cast<UMaterialInstanceDynamic>(ParentMaterial)) {
        UMaterialInterface* NextParent=DynamicParent->Parent;
        if(!IsValid(NextParent) || NextParent==ParentMaterial) return nullptr;
        ParentMaterial=NextParent;
    }
    auto* Result=UMaterialInstanceDynamic::Create(ParentMaterial,Outer);
    if(Result && SourceDynamic) Result->CopyInterpParameters(SourceDynamic);
    return Result;
}
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
    BridgeCamera->bUsePawnControlRotation = true;BridgeCamera->bConstrainAspectRatio=true;
    BridgeCamera->PostProcessSettings.bOverride_MotionBlurAmount=true;BridgeCamera->PostProcessSettings.MotionBlurAmount=0;
    BridgeCamera->PostProcessBlendWeight=1;
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
    GetCharacterMovement()->MaxStepHeight=float(BridgeCharacterMath::StepHeightCm);
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
    SkinSleeve=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MinecraftFirstPersonSleeve"));PrepareSkin(SkinSleeve,BridgeCamera);SkinSleeve->SetCastShadow(false);
    ProjectedSleeve=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProjectedFallbackSleeve"));PrepareSkin(ProjectedSleeve,BridgeCamera);ProjectedSleeve->SetCastShadow(false);
    ProjectedHand=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProjectedFallbackHand"));PrepareSkin(ProjectedHand,BridgeCamera);ProjectedHand->SetCastShadow(false);
    HeldModel=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MinecraftHeldModel"));PrepareSkin(HeldModel,BridgeCamera);HeldModel->SetCastShadow(false);
    OffhandModel=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MinecraftOffhandModel"));PrepareSkin(OffhandModel,BridgeCamera);OffhandModel->SetCastShadow(false);
    AvatarRoot->SetVisibility(false,true);SkinArm->SetVisibility(false,true);
    AimRoot=CreateDefaultSubobject<USceneComponent>(TEXT("AimOutline"));AimRoot->SetupAttachment(GetCapsuleComponent());
    AimOutline=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("NativeBlockOutline"));PrepareSkin(AimOutline,AimRoot);AimOutline->SetCastShadow(false);
    AimRoot->SetVisibility(false,true);HeldMesh->SetVisibility(false);
    ProjectedSleeve->SetVisibility(false);ProjectedHand->SetVisibility(false);HeldModel->SetVisibility(false);
    OffhandModel->SetVisibility(false);
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
    UEAuthority=Enabled; BridgeFlying=false; FlightWasAirborne=FlightLandingLatch=false; PreviousJump=false; SprintRequested=false;BodyYawInitialized=false;StopJumping();
    if(!Enabled) {
        NativePresentation=false;NativePendingVisual=NativeEquipLowering=NativeUsingItem=false;
        OffhandPendingVisual=false;OffhandItem.Empty();OffhandGeometryReady=false;
        BlockOutlineReachCm=500;
    }
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
    if(Sneak && !BridgeFlying) Crouch(); else UnCrouch();
    SprintRequested=Sprint && !Sneak && Forward>0;
    GetCharacterMovement()->MaxWalkSpeed=SprintRequested ? 561.2f : 431.7f;
    GetCharacterMovement()->MaxFlySpeed=SprintRequested ? 2160.f : 1080.f;
    const FRotator Heading(0,GetControlRotation().Yaw,0);
    FVector Direction=Heading.Vector()*Forward + FRotationMatrix(Heading).GetUnitAxis(EAxis::Y)*Right;
    const float Magnitude=FMath::Min(1.f,Direction.Size());
    if(Magnitude>0) AddMovementInput(Direction.GetSafeNormal(),Magnitude);
    if(BridgeFlying) {
        AddMovementInput(FVector::UpVector,(JumpHeld ? 1.f : 0.f)-(Sneak ? 1.f : 0.f));
        StopJumping();
    } else if(JumpHeld && (!PreviousJump || GetCharacterMovement()->IsMovingOnGround())) Jump();
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
void ABridgeCharacter::ApplyFlight(bool Creative,bool Flying) {
    if(!Flying || !Creative || !UEAuthority) FlightLandingLatch=false;
    const bool Enabled=UEAuthority && Creative && Flying && !FlightLandingLatch;
    if(BridgeFlying==Enabled) return;
    BridgeFlying=Enabled;FlightWasAirborne=false;StopJumping();PreviousJump=false;
    auto* Movement=GetCharacterMovement();Movement->StopMovementImmediately();
    Movement->MaxFlySpeed=1080.f;Movement->BrakingDecelerationFlying=6500.f;
    Movement->SetMovementMode(Enabled ? MOVE_Flying : (UEAuthority ? MOVE_Falling : MOVE_None));
}
void ABridgeCharacter::ConfigureOutline(UMaterialInterface* Material) {
    if(Material && AimOutline->GetMaterial(0)!=Material) AimOutline->SetMaterial(0,Material);
}
float ABridgeCharacter::GetHandSwing() const {
    if(UEAuthority) return SwingRemaining>0.f ? 1.f-SwingRemaining/float(BridgeCharacterMath::SwingSeconds) : 0.f;
    return HasPlayerVisuals ? PlayerSwing : (SwingRemaining>0.f ? 1.f-SwingRemaining/float(BridgeCharacterMath::SwingSeconds) : 0.f);
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
    BridgeVerticalFov=MinecraftBaseFov*(1.f+(FovSprintMultiplier-1.f)*NativeFovEffectScale);
    BridgeCamera->SetFieldOfView(float(BridgeCharacterMath::HorizontalFov(BridgeVerticalFov,BridgeCamera->AspectRatio)));
    SwingRemaining=FMath::Max(0.f,SwingRemaining-DeltaSeconds);
    const float ViewYaw=GetControlRotation().Yaw;
    if(!BodyYawInitialized) {BridgeBodyYaw=ViewYaw;BodyYawInitialized=true;}
    BridgeBodyYaw=float(BridgeCharacterMath::BodyYaw(BridgeBodyYaw,ViewYaw,GetVelocity().Rotation().Yaw,
        GetVelocity().Size2D(),GetHandSwing()>0.f,PlayerUsingItem && PlayerUseAction==TEXT("block"),DeltaSeconds));
    LimbAmplitude=float(BridgeCharacterMath::Smooth(LimbAmplitude,FMath::Min(1.f,GetVelocity().Size2D()/500.f),8.f,DeltaSeconds));
    const bool Backwards=FVector::DotProduct(GetVelocity(),FRotator(0,BridgeBodyYaw,0).Vector())<0.f;
    BobPhase+=DeltaSeconds*20.f*.6662f*LimbAmplitude*(Backwards ? -1.f : 1.f);
    const float Bob=NativeBobView && UEAuthority && GetCharacterMovement()->IsMovingOnGround() ? FMath::Sin(BobPhase)*FMath::Min(1.f,GetVelocity().Size2D()/432.f) : 0;
    HandBob=float(BridgeCharacterMath::Smooth(HandBob,Bob,14.f,DeltaSeconds));
    if(BridgeFlying) {
        FFindFloorResult Floor;GetCharacterMovement()->FindFloor(GetActorLocation(),Floor,false);
        const bool OnFloor=Floor.IsWalkableFloor() && Floor.GetDistanceToFloor()<=2.5f;
        if(!OnFloor) FlightWasAirborne=true;
        // The first takeoff frame still touches the floor before movement consumes
        // the jump-key input. Do not mistake it for a landing.
        if(OnFloor && FlightWasAirborne && GetVelocity().Z<=0) {
            ApplyFlight(true,false);
            // Input remains flying until the ground pose reaches Minecraft. Keep
            // falling/walking long enough for that ACK, rather than re-enabling.
            FlightLandingLatch=true;
        }
    }
    UpdatePlayerCamera();UpdateAvatar(HandBob);
    FIntVector Block;FVector Normal;
    FVector EyePosition;FRotator AimRotation;GetEyeAim(EyePosition,AimRotation);
    const bool Aimed=UEAuthority && IsValid(InteractionWorld) && InteractionWorld->Aim(EyePosition,AimRotation,BlockOutlineReachCm,Block,Normal,this);
    AimRoot->SetVisibility(Aimed,true);
    if(Aimed) {
        AimRoot->SetWorldLocationAndRotation(InteractionWorld->BlockCenter(Block),FRotator::ZeroRotator);
        FString Id,State;InteractionWorld->GetBlockState(Block,Id,State);
        if(!AimShapeReady || AimVoxel!=Block || AimState!=Id+TEXT("[")+State+TEXT("]")) {
            AimVoxel=Block;AimState=Id+TEXT("[")+State+TEXT("]");AimShapeReady=true;
            TArray<FBox> Boxes;InteractionWorld->GetBlockOutline(Block,Boxes);
            std::vector<BridgeOutlineMath::Box> Hulls;
            for(const auto& Box:Boxes) Hulls.push_back({{Box.Min.X,Box.Min.Y,Box.Min.Z},{Box.Max.X,Box.Max.Y,Box.Max.Z}});
            const auto Lines=BridgeOutlineMath::Edges(Hulls);
            TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
            auto Convert=[](const BridgeOutlineMath::Point& P){return FVector(P[2]-.5,-P[0]+.5,P[1]-.5)*100;};
            for(const auto& Line:Lines) {
                const FVector A=Convert(Line.A),B=Convert(Line.B),D=(B-A).GetSafeNormal();
                const FVector U=(FMath::Abs(D.Z)<.9 ? FVector::CrossProduct(D,FVector::UpVector) : FVector::CrossProduct(D,FVector::ForwardVector)).GetSafeNormal()*.25;
                const FVector V=FVector::CrossProduct(D,U);
                const FVector Corners[]={U+V,-U+V,-U-V,U-V};
                for(int32 I=0;I<4;++I) {
                    const int32 Base=Vertices.Num();
                    Vertices.Append({A+Corners[I],B+Corners[I],B+Corners[(I+1)%4],A+Corners[(I+1)%4]});
                    Indices.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
                    for(int32 J=0;J<4;++J) {Normals.Add(Corners[I].GetSafeNormal());UV.Add(FVector2D::ZeroVector);Colors.Add(FLinearColor::Black);}
                }
            }
            AimOutline->ClearAllMeshSections();
            if(!Vertices.IsEmpty()) AimOutline->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
        }

    }
}

void ABridgeCharacter::ConfigureVisuals(UMaterialInterface* Material,UBridgeBlockPalette* Palette,const FString& Item,const FString& Block,int32 Color,const FString& ModelKey) {
    if(NativePresentation && !NativeApplyingVisual && VisualsConfigured && (VisualItem!=Item || VisualModelKey!=ModelKey)) {
        NativePendingVisual=true;NativeEquipLowering=true;
        NativePendingMaterial=Material;NativePendingPalette=Palette;NativePendingItem=Item;
        NativePendingBlock=Block;NativePendingColor=Color;NativePendingModel=ModelKey;
        return;
    }
    if(!VisualsConfigured || VisualMaterial!=Material) {
        auto Tint=[&](UStaticMeshComponent* VisualMesh,const FColor& ColorValue) {
            UMaterialInterface* Base=Material ? Material : VisualMesh->GetMaterial(0);if(!Base) return;
            auto* Dynamic=CreateVisualInstance(Base,this);if(!Dynamic) return;
            Dynamic->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(ColorValue));
            VisualMesh->SetMaterial(0,Dynamic);
        };
        Tint(Sleeve,FColor(45,100,165));Tint(Hand,FColor(199,150,113));
        if(!AimOutline->GetMaterial(0) || AimOutline->GetMaterial(0)==VisualMaterial) {
            auto* Outline=CreateVisualInstance(Material,this);if(Outline) {Outline->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::Black);AimOutline->SetMaterial(0,Outline);}
        }
    }
    if(!VisualsConfigured || VisualMaterial!=Material || VisualPalette!=Palette || VisualItem!=Item || VisualBlock!=Block || VisualColor!=Color || VisualModelKey!=ModelKey) {
        HeldGeometryReady=false;
        if(VisualMaterial!=Material || VisualPalette!=Palette) OffhandGeometryReady=false;
        UMaterialInterface* ItemMaterial=Palette && !Block.IsEmpty() ? Palette->Find(Block) : nullptr;
        if(!ItemMaterial) ItemMaterial=Material ? Material : Hand->GetMaterial(0);
        if(ItemMaterial) {
            auto* Dynamic=CreateVisualInstance(ItemMaterial,this);
            const FColor ItemColor(Block.IsEmpty() ? 200 : ((Color>>16)&255),Block.IsEmpty() ? 180 : ((Color>>8)&255),Block.IsEmpty() ? 110 : (Color&255));
            if(Dynamic) {Dynamic->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(ItemColor));HeldMesh->SetMaterial(0,Dynamic);}
        }
    }
    VisualsConfigured=true;VisualMaterial=Material;VisualPalette=Palette;VisualItem=Item;VisualBlock=Block;VisualColor=Color;VisualModelKey=ModelKey;
}

void ABridgeCharacter::ConfigureOffhandVisuals(const FString& Item,const FString& Block,int32 Color,const FString& ModelKey) {
    if(OffhandItem==Item && OffhandBlock==Block && OffhandColor==Color && OffhandModelKey==ModelKey) {
        OffhandPendingVisual=false;return;
    }
    if(NativePresentation) {
        OffhandPendingVisual=true;PendingOffhandItem=Item;PendingOffhandBlock=Block;
        PendingOffhandColor=Color;PendingOffhandModelKey=ModelKey;
    } else {
        OffhandItem=Item;OffhandBlock=Block;OffhandColor=Color;OffhandModelKey=ModelKey;OffhandGeometryReady=false;
    }
}

void ABridgeCharacter::SetInteractionWorld(ABridgeWorld* Imported) { InteractionWorld=Imported; }
void ABridgeCharacter::SetNativeBlockReach(float ReachCm) {
    BlockOutlineReachCm=FMath::IsFinite(ReachCm) ? FMath::Clamp(ReachCm,0.f,1000.f) : 500.f;
}

void ABridgeCharacter::ConfigureAppearance(UBridgePlayerAppearance* Appearance) {
    if(PlayerAppearance==Appearance && AvatarGeometryReady) return;
    PlayerAppearance=Appearance;AvatarGeometryReady=false;
    if(Appearance && !HasPlayerVisuals) PlayerSlim=Appearance->IsSlim;
    BuildAvatarGeometry();
}

void ABridgeCharacter::ApplyPlayerVisuals(int32 Perspective,float SwingProgress,float EquipProgress,bool UsingItem,const FString& UseAction,float UseProgress,bool LeftHanded,int32 SkinLayers,bool SlimArms) {
    if((CameraPerspective==0)!=(Perspective==0) || PlayerLeftHanded!=LeftHanded) {HeldGeometryReady=false;OffhandGeometryReady=false;}
    CameraPerspective=FMath::Clamp(Perspective,0,2);
    PlayerSwing=FMath::Clamp(SwingProgress,0.f,1.f);PlayerEquip=FMath::Clamp(EquipProgress,0.f,1.f);
    PlayerUsingItem=UsingItem;PlayerUseAction=UseAction;PlayerUseProgress=FMath::Clamp(UseProgress,0.f,1.f);
    PlayerSkinLayers=SkinLayers&127;
    const bool Changed=PlayerLeftHanded!=LeftHanded || PlayerSlim!=SlimArms;
    PlayerLeftHanded=LeftHanded;PlayerSlim=SlimArms;HasPlayerVisuals=true;
    if(Changed) AvatarGeometryReady=false;
    UpdatePlayerCamera();
}

void ABridgeCharacter::ApplyNativePresentation(int32 Perspective,bool LeftHanded,int32 SkinLayers,bool SlimArms,float DeltaSeconds) {
    NativePresentation=true;
    // Vanilla equips at a maximum of .4 per game tick. Advance locally every UE
    // frame so direct play does not inherit the old Fabric pose send frequency.
    PlayerEquip=FMath::FInterpConstantTo(PlayerEquip,NativeEquipLowering ? 0.f : 1.f,DeltaSeconds,8.f);
    OffhandEquip=FMath::FInterpConstantTo(OffhandEquip,OffhandPendingVisual ? 0.f : 1.f,DeltaSeconds,8.f);
    if(OffhandPendingVisual && OffhandEquip<=.01f) {
        OffhandItem=PendingOffhandItem;OffhandBlock=PendingOffhandBlock;OffhandColor=PendingOffhandColor;
        OffhandModelKey=PendingOffhandModelKey;OffhandGeometryReady=false;OffhandPendingVisual=false;
    }
    if(NativeEquipLowering && PlayerEquip<=.01f) {
        NativeEquipLowering=false;
        if(NativePendingVisual) {
            NativeApplyingVisual=true;
            ConfigureVisuals(NativePendingMaterial,NativePendingPalette,NativePendingItem,NativePendingBlock,NativePendingColor,NativePendingModel);
            NativeApplyingVisual=false;NativePendingVisual=false;
        }
    }
    ApplyPlayerVisuals(Perspective,0,PlayerEquip,NativeUsingItem,NativeUseAction,NativeUseProgress,LeftHanded,SkinLayers,SlimArms);
}

void ABridgeCharacter::SetNativeUse(bool Using,float Progress,const FString& Action) {
    NativeUsingItem=Using;NativeUseProgress=FMath::Clamp(Progress,0.f,1.f);NativeUseAction=Using ? Action : TEXT("none");
}

void ABridgeCharacter::GetEyeAim(FVector& EyePosition,FRotator& AimRotation) const {
    const float EyeHeight=UEAuthority ? float(bIsCrouched ? BridgeCharacterMath::CrouchedEyeCm : BridgeCharacterMath::StandingEyeCm) : RemoteEyeHeight;
    EyePosition=GetMinecraftFeetPosition()+FVector(0,0,EyeHeight);
    AimRotation=GetControlRotation();
}

void ABridgeCharacter::SetMinecraftFov(float VerticalFov) {
    BridgeCamera->AspectRatio=16.f/9.f;
    MinecraftBaseFov=FMath::IsFinite(VerticalFov) ? FMath::Clamp(VerticalFov,30.f,110.f) : 80.f;
    BridgeVerticalFov=MinecraftBaseFov*(1.f+(FovSprintMultiplier-1.f)*NativeFovEffectScale);
    BridgeCamera->SetFieldOfView(float(BridgeCharacterMath::HorizontalFov(BridgeVerticalFov,BridgeCamera->AspectRatio)));
}

void ABridgeCharacter::ConfigureNativeViewOptions(bool BobView,float FovEffectScale) {
    NativeBobView=BobView;
    NativeFovEffectScale=FMath::IsFinite(FovEffectScale) ? FMath::Clamp(FovEffectScale,0.f,1.f) : 1.f;
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
    CacheHandGeometry(SkinArm);CacheHandGeometry(SkinSleeve);
    // Match the skinned arm's 75 cm silhouette when no personal skin is imported.
    SkinCuboid(ProjectedSleeve,ArmWidth,7.2f,4,0,0,0);
    SkinCuboid(ProjectedHand,ArmWidth,4.8f,4,0,0,0);
    CacheHandGeometry(ProjectedSleeve);CacheHandGeometry(ProjectedHand);
}

void ABridgeCharacter::CacheHandGeometry(UProceduralMeshComponent* Part) {
    auto& Sources=HandSources.FindOrAdd(Part);Sources.Empty();
    for(int32 Index=0;Index<Part->GetNumSections();++Index) {
        const auto* Section=Part->GetProcMeshSection(Index);
        auto& Source=Sources.AddDefaulted_GetRef();
        if(!Section) continue;
        for(const auto& Vertex:Section->ProcVertexBuffer) {
            Source.Positions.Add(Vertex.Position);Source.Normals.Add(Vertex.Normal);
            Source.UV.Add(Vertex.UV0);Source.Colors.Add(FLinearColor::FromSRGBColor(Vertex.Color));
            Source.Tangents.Add(Vertex.Tangent);
        }
    }
}

void ABridgeCharacter::PoseHandGeometry(UProceduralMeshComponent* Part,const FTransform& Pose,bool FixedHandFov) {
    const auto* Sources=HandSources.Find(Part);if(!Sources) return;
    Part->SetRelativeTransform(FTransform::Identity);
    const double Transverse=FixedHandFov ? BridgeCharacterMath::FirstPersonTransverseScale(BridgeVerticalFov) : 1.0;
    const FVector Stretch(1,Transverse,Transverse),Scale=Pose.GetScale3D();
    for(int32 Index=0;Index<Sources->Num();++Index) {
        const auto& Source=(*Sources)[Index];if(Source.Positions.IsEmpty()) continue;
        TArray<FVector> Positions,Normals;TArray<FProcMeshTangent> Tangents;
        Positions.Reserve(Source.Positions.Num());Normals.Reserve(Source.Positions.Num());Tangents.Reserve(Source.Positions.Num());
        for(int32 V=0;V<Source.Positions.Num();++V) {
            Positions.Add(Pose.TransformPosition(Source.Positions[V])*Stretch);
            // Inverse transpose, including the display scale of narrow items.
            const FVector N=Pose.GetRotation().RotateVector(Source.Normals[V]/Scale)/Stretch;
            const FVector Normal=N.GetSafeNormal();Normals.Add(Normal);
            const FVector T=Pose.TransformVector(Source.Tangents[V].TangentX)*Stretch;
            Tangents.Add(FProcMeshTangent((T-Normal*FVector::DotProduct(T,Normal)).GetSafeNormal(),Source.Tangents[V].bFlipTangentY));
        }
        Part->UpdateMeshSection_LinearColor(Index,Positions,Normals,Source.UV,Source.Colors,Tangents);
    }
}

void ABridgeCharacter::BuildHeldGeometry() {
    BuildHandGeometry(HeldModel,VisualItem,VisualBlock,VisualColor,VisualModelKey,PlayerLeftHanded,NativeHeldGeometry,HeldModelStatus);
    HeldGeometryReady=true;
}

void ABridgeCharacter::BuildHandGeometry(UProceduralMeshComponent* Model,const FString& Item,const FString& Block,int32 Color,const FString& ModelKey,bool LeftHanded,bool& NativeGeometry,FString& Status) {
    Model->ClearAllMeshSections();
    TArray<FBridgeModelFace> Faces;
    const FString Context=CameraPerspective==0 ? (LeftHanded ? TEXT("firstperson_lefthand") : TEXT("firstperson_righthand"))
        : (LeftHanded ? TEXT("thirdperson_lefthand") : TEXT("thirdperson_righthand"));
    NativeGeometry=IsValid(VisualPalette) && VisualPalette->BuildItem(ModelKey.IsEmpty() ? Item : ModelKey,Context,Faces);
    const bool Imported=NativeGeometry || (ModelKey.IsEmpty() && IsValid(VisualPalette) && !Block.IsEmpty()
        && VisualPalette->BuildModel(Block,VisualPalette->DefaultState(Block),Faces) && !Faces.IsEmpty());
    if(!Imported && !Item.IsEmpty()) {Status=TEXT("item model missing: ")+Item+TEXT("; /uebridge items export then import");CacheHandGeometry(Model);return;}
    Status=Item.IsEmpty() ? TEXT("empty") : (NativeGeometry ? TEXT("native item: ")+Item : TEXT("block model: ")+Item);
    if(!Imported) {
        // Empty-hand placeholder; it is hidden by UpdateAvatar. Missing selected
        // item models returned above with an explicit diagnostic.
        const FVector N[]={FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)};
        for(const FVector& Normal:N) {
            const FVector U=FMath::Abs(Normal.Z)>.5f ? FVector(1,0,0) : FVector::CrossProduct(FVector(0,0,1),Normal);
            const FVector V=FVector::CrossProduct(Normal,U),Center=Normal*.5f;
            FBridgeModelFace Face;
            const FVector Positions[]={Center-U*.5f-V*.5f,Center+U*.5f-V*.5f,Center+U*.5f+V*.5f,Center-U*.5f+V*.5f};
            const FVector2D UV[]={FVector2D(0,1),FVector2D(1,1),FVector2D(1,0),FVector2D(0,0)};
            for(int32 I=0;I<4;++I) {Face.Vertices[I]=Positions[I]+FVector(.5);Face.UV[I]=UV[I];}
            Faces.Add(MoveTemp(Face));
        }
    }
    struct FModelSection {
        TArray<FVector> Vertices,Normals;
        TArray<FVector2D> UV;
        TArray<int32> Triangles;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        UMaterialInterface* Material=nullptr;FColor Tint=FColor::White;
    };
    TArray<FModelSection> Sections;TMap<FString,int32> MaterialSections;
    for(const auto& Face:Faces) {
        const FString Key=Imported ? Face.TextureId+(Face.bTint ? TEXT("#1") : TEXT("#0"))+FString::Printf(TEXT("#%u"),Face.Color.ToPackedARGB()) : TEXT("legacy");
        int32* Existing=MaterialSections.Find(Key);int32 Index;
        if(Existing) Index=*Existing;
        else {
            Index=Sections.Num();MaterialSections.Add(Key,Index);auto& New=Sections.AddDefaulted_GetRef();
            New.Tint=NativeGeometry ? Face.Color : FColor((Color>>16)&255,(Color>>8)&255,Color&255);
            if(NativeGeometry) {const auto* ItemMaterial=VisualPalette->ItemMaterials.Find(Face.TextureId);New.Material=ItemMaterial ? ItemMaterial->Get() : nullptr;}
            else New.Material=Imported ? VisualPalette->FindFaceMaterial(Face.TextureId,Face.bTint) : HeldMesh->GetMaterial(0);
        }
        auto& Section=Sections[Index];const int32 First=Section.Vertices.Num();
        for(int32 I=0;I<4;++I) {
            const FVector MC=NativeGeometry ? Face.Vertices[I] : Face.Vertices[I]-FVector(.5);
            Section.Vertices.Add((NativeGeometry ? FVector(-MC.Z,MC.X,MC.Y) : FVector(MC.Z,-MC.X,MC.Y))*100);Section.UV.Add(Face.UV[I]);Section.Colors.Add(FLinearColor::White);
        }
        const FVector Normal=FVector::CrossProduct(Section.Vertices[First+2]-Section.Vertices[First],Section.Vertices[First+1]-Section.Vertices[First]).GetSafeNormal();
        const FVector Tangent=(Section.Vertices[First+1]-Section.Vertices[First]).GetSafeNormal();
        for(int32 I=0;I<4;++I) {Section.Normals.Add(Normal);Section.Tangents.Add(FProcMeshTangent(Tangent,false));}
        Section.Triangles.Append({First,First+2,First+1,First,First+3,First+2});
    }
    for(int32 Index=0;Index<Sections.Num();++Index) {
        const auto& Section=Sections[Index];
        Model->CreateMeshSection_LinearColor(Index,Section.Vertices,Section.Triangles,Section.Normals,Section.UV,Section.Colors,Section.Tangents,false);
        if(Section.Material) {
            auto* Dynamic=CreateVisualInstance(Section.Material,this);
            const FColor ItemTint=Section.Tint;
            if(Dynamic) {Dynamic->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(ItemTint));Model->SetMaterial(Index,Dynamic);}
        }
    }
    CacheHandGeometry(Model);
}

void ABridgeCharacter::UpdateAvatar(float Bob) {
    if(!AvatarGeometryReady) BuildAvatarGeometry();
    if(!HeldGeometryReady) BuildHeldGeometry();
    if(!OffhandGeometryReady) {
        BuildHandGeometry(OffhandModel,OffhandItem,OffhandBlock,OffhandColor,OffhandModelKey,!PlayerLeftHanded,NativeOffhandGeometry,OffhandModelStatus);
        OffhandGeometryReady=true;
    }
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
    const int32 ActiveArm=PlayerLeftHanded ? 3 : 2;
    if(!VisualItem.IsEmpty()) {
        FRotator Carry=AvatarParts[ActiveArm]->GetRelativeRotation();
        Carry.Pitch=Carry.Pitch*.5f+18.f;
        AvatarParts[ActiveArm]->SetRelativeRotation(Carry);
    }
    if(!OffhandItem.IsEmpty()) {
        const int32 OtherArm=PlayerLeftHanded ? 2 : 3;
        FRotator Carry=AvatarParts[OtherArm]->GetRelativeRotation();Carry.Pitch=Carry.Pitch*.5f+18.f;
        AvatarParts[OtherArm]->SetRelativeRotation(Carry);
    }
    // Empty-hand attacks animate in third person too. Previously this was inside
    // the held-item branch, so punching with an empty hotbar slot never moved.
    if(NativeSwing>0) AvatarParts[ActiveArm]->AddLocalRotation(FRotator(float(BridgeCharacterMath::AttackPitch(NativeSwing,GetControlRotation().Pitch)),0,0));
    const float Side=PlayerLeftHanded ? -1.f : 1.f;
    const auto ArmPose=BridgeCharacterMath::FirstPersonArm(NativeSwing,PlayerEquip,PlayerLeftHanded,PlayerSlim);
    const auto ItemPose=NativeHeldGeometry ? BridgeCharacterMath::FirstPersonItem(NativeSwing,PlayerEquip,PlayerLeftHanded) : BridgeCharacterMath::FirstPersonBlock(NativeSwing,PlayerEquip,PlayerLeftHanded);
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
    Sleeve->SetVisibility(false);Hand->SetVisibility(false);HeldMesh->SetVisibility(false);
    ProjectedSleeve->SetVisibility(FirstPerson && !HasSkin && EmptyHand);ProjectedHand->SetVisibility(FirstPerson && !HasSkin && EmptyHand);
    ProjectedSleeve->SetMaterial(0,Sleeve->GetMaterial(0));ProjectedHand->SetMaterial(0,Hand->GetMaterial(0));
    if(FirstPerson && EmptyHand) {
        const FTransform ArmTransform(ArmRotation,ArmPosition);
        if(HasSkin) {
            PoseHandGeometry(SkinArm,ArmTransform,true);PoseHandGeometry(SkinSleeve,ArmTransform,true);
        } else {
            PoseHandGeometry(ProjectedSleeve,ArmTransform,true);
            PoseHandGeometry(ProjectedHand,FTransform(ArmRotation,ArmPosition+ArmRotation.RotateVector(FVector(0,0,-45.f))),true);
        }
    }
    HeldModel->SetVisibility((FirstPerson || ThirdPerson) && !VisualItem.IsEmpty());
    if(ThirdPerson) {
        const int32 ArmIndex=PlayerLeftHanded ? 3 : 2;
        HeldModel->AttachToComponent(AvatarParts[ArmIndex],FAttachmentTransformRules::KeepRelativeTransform);
        const auto GripPose=NativeHeldGeometry ? BridgeCharacterMath::ThirdPersonItem(PlayerLeftHanded) : BridgeCharacterMath::ThirdPersonBlock(PlayerLeftHanded);
        PoseHandGeometry(HeldModel,FTransform(PoseRotation(GripPose),PosePosition(GripPose),NativeHeldGeometry ? FVector(1) : FVector(BridgeCharacterMath::ThirdPersonBlockScale)),false);
    } else if(FirstPerson) {
        HeldModel->AttachToComponent(BridgeCamera,FAttachmentTransformRules::KeepRelativeTransform);
        PoseHandGeometry(HeldModel,FTransform(ItemRotation,ItemPosition,NativeHeldGeometry ? FVector(1) : FVector(BridgeCharacterMath::FirstPersonBlockScale)),true);
    }
    OffhandModel->SetVisibility(NativePresentation && (FirstPerson || ThirdPerson) && !OffhandItem.IsEmpty());
    if(NativePresentation && !OffhandItem.IsEmpty()) {
        if(ThirdPerson) {
            const int32 OtherArm=PlayerLeftHanded ? 2 : 3;
            OffhandModel->AttachToComponent(AvatarParts[OtherArm],FAttachmentTransformRules::KeepRelativeTransform);
            const auto GripPose=NativeOffhandGeometry ? BridgeCharacterMath::ThirdPersonItem(!PlayerLeftHanded) : BridgeCharacterMath::ThirdPersonBlock(!PlayerLeftHanded);
            PoseHandGeometry(OffhandModel,FTransform(PoseRotation(GripPose),PosePosition(GripPose),NativeOffhandGeometry ? FVector(1) : FVector(BridgeCharacterMath::ThirdPersonBlockScale)),false);
        } else if(FirstPerson) {
            OffhandModel->AttachToComponent(BridgeCamera,FAttachmentTransformRules::KeepRelativeTransform);
            const auto OffhandPose=NativeOffhandGeometry ? BridgeCharacterMath::FirstPersonItem(0,OffhandEquip,!PlayerLeftHanded) : BridgeCharacterMath::FirstPersonBlock(0,OffhandEquip,!PlayerLeftHanded);
            PoseHandGeometry(OffhandModel,FTransform(PoseRotation(OffhandPose),PosePosition(OffhandPose)+FVector(0,0,Bob),NativeOffhandGeometry ? FVector(1) : FVector(BridgeCharacterMath::FirstPersonBlockScale)),true);
        }
    }
}
