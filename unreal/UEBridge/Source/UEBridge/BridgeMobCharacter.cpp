#include "BridgeMobCharacter.h"
#include "BridgeMobWorld.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "BridgeCombatMath.h"
#include "Materials/MaterialInstanceDynamic.h"

ABridgeMobCharacter::ABridgeMobCharacter() {
    PrimaryActorTick.bCanEverTick=true;
    bUseControllerRotationYaw=false;
    VisualRoot=CreateDefaultSubobject<USceneComponent>(TEXT("MinecraftMobModel"));VisualRoot->SetupAttachment(GetCapsuleComponent());
    GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    auto* Movement=GetCharacterMovement();
    Movement->bRunPhysicsWithNoController=true;Movement->bOrientRotationToMovement=true;
    Movement->RotationRate=FRotator(0,360,0);Movement->MaxStepHeight=60;Movement->JumpZVelocity=900;
    Movement->AirControl=.2f;Movement->MaxAcceleration=1800;Movement->BrakingDecelerationWalking=1200;
    Tags.Add(TEXT("BridgeMinecraftVisual"));
}

bool ABridgeMobCharacter::Initialize(const FBridgeMobSnapshot& Snapshot,const FBridgeMobAppearance& Appearance,ABridgeMobWorld* OwnerWorld) {
    auto Invalid=[&](const FString& Reason){InitializationReason=Reason;return false;};
    VertexCount=0;
    if(!Appearance.Material) return Invalid(TEXT("material_missing"));
    if(Appearance.Parts.IsEmpty() || Appearance.Parts.Num()>256) return Invalid(TEXT("part_count"));
    if(Appearance.RenderScale.ContainsNaN() || Appearance.RenderScale.GetMin()<=0 || Appearance.RenderOffset.ContainsNaN()) return Invalid(TEXT("renderer_transform"));
    // Validate the complete hierarchy before creating components. Root/anchor parts may have no cuboids.
    for(int32 Index=0;Index<Appearance.Parts.Num();Index++) {
        const FBridgeMobPart& Part=Appearance.Parts[Index];
        if(Part.Parent>=Index || Part.Parent< -1) return Invalid(FString::Printf(TEXT("parent index %d->%d"),Index,Part.Parent));
        if(Part.Rest.ContainsNaN()) return Invalid(FString::Printf(TEXT("rest transform part %d"),Index));
        if(Part.Vertices.Num()!=Part.Texcoords.Num() || Part.Vertices.Num()%4 || Part.Vertices.Num()>16384) return Invalid(FString::Printf(TEXT("quad/UV count part %d"),Index));
        for(const FVector& Vertex:Part.Vertices) if(Vertex.ContainsNaN()) return Invalid(FString::Printf(TEXT("vertex part %d"),Index));
        for(const FVector2D& UV:Part.Texcoords) if(UV.ContainsNaN()) return Invalid(FString::Printf(TEXT("UV part %d"),Index));
        for(const FTransform& Pose:Part.WalkFrames) if(Pose.ContainsNaN()) return Invalid(FString::Printf(TEXT("walk transform part %d"),Index));
        VertexCount+=Part.Vertices.Num();
    }
    if(VertexCount==0) return Invalid(TEXT("no_visible_model_cuboids"));
    MinecraftId=Snapshot.Id;MinecraftType=Snapshot.Type;Health=Snapshot.Health;MaxHealth=Snapshot.MaxHealth;
    InitialSnapshot=Snapshot;
    Hostile=Snapshot.Hostile;Damage=Snapshot.Damage;WorldOwner=OwnerWorld;
    float HalfHeight=FMath::Clamp(Snapshot.Height*50.f,10.f,1000.f);
    float Radius=FMath::Clamp(Snapshot.Width*50.f,5.f,HalfHeight);
    GetCapsuleComponent()->SetCapsuleSize(Radius,HalfHeight);
    VisualRoot->SetRelativeLocation(Appearance.RenderOffset+FVector(0,0,150.1f*Appearance.RenderScale.Z-HalfHeight));
    VisualRoot->SetRelativeScale3D(Appearance.RenderScale);
    auto* Movement=GetCharacterMovement();
    const float Gravity=GetWorld() ? FMath::Abs(GetWorld()->GetGravityZ()) : 980.f;
    Movement->GravityScale=3200.f/FMath::Max(1.f,Gravity);
    // Vanilla MOVEMENT_SPEED .25 gives about 4.3 blocks/s on level ground.
    GroundSpeed=FMath::Clamp(Snapshot.Speed*1727.f,0.f,1200.f);Movement->MaxWalkSpeed=GroundSpeed;
    Movement->JumpZVelocity=840;Movement->AirControl=0.02f;
    Movement->MaxAcceleration=GroundSpeed*12.1f;Movement->GroundFriction=12.1f;
    Movement->BrakingFrictionFactor=1;Movement->BrakingDecelerationWalking=0;
    Movement->FallingLateralFriction=1.886f;Movement->BrakingDecelerationFalling=0;
    Random.Initialize(int32(GetTypeHash(Snapshot.Id)));
    Poses=Appearance.Parts;
    for(int32 Index=0;Index<Poses.Num();Index++) {
        const FBridgeMobPart& Part=Poses[Index];
        UProceduralMeshComponent* MobPartComponent=NewObject<UProceduralMeshComponent>(this,FName(*FString::Printf(TEXT("MobPart_%d"),Index)));
        AddInstanceComponent(MobPartComponent);MobPartComponent->SetupAttachment(Part.Parent<0 ? VisualRoot.Get() : Parts[Part.Parent].Get());
        MobPartComponent->SetRelativeTransform(Part.Rest);MobPartComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MobPartComponent->SetGenerateOverlapEvents(false);MobPartComponent->SetCastShadow(true);MobPartComponent->ComponentTags.Add(TEXT("BridgeMinecraftVisual"));
        MobPartComponent->RegisterComponent();Parts.Add(MobPartComponent);
        TArray<int32> Triangles;TArray<FVector> Normals;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
        Normals.SetNum(Part.Vertices.Num());Colors.Init(FLinearColor::White,Part.Vertices.Num());
        for(int32 Vertex=0;Vertex<Part.Vertices.Num();Vertex+=4) {
            // The Minecraft right-handed to Unreal left-handed conversion reverses winding.
            Triangles.Append({Vertex,Vertex+2,Vertex+1,Vertex,Vertex+3,Vertex+2});
            FVector Normal=FVector::CrossProduct(Part.Vertices[Vertex+2]-Part.Vertices[Vertex],Part.Vertices[Vertex+1]-Part.Vertices[Vertex]).GetSafeNormal();
            for(int32 Offset=0;Offset<4;Offset++) Normals[Vertex+Offset]=Normal;
        }
        if(!Part.Vertices.IsEmpty()) MobPartComponent->CreateMeshSection_LinearColor(0,Part.Vertices,Triangles,Normals,Part.Texcoords,Colors,Tangents,false);
        MobPartComponent->SetMaterial(0,UMaterialInstanceDynamic::Create(Appearance.Material,MobPartComponent));
    }
    LastPosition=GetActorLocation();GetCharacterMovement()->SetMovementMode(MOVE_None);InitializationReason=TEXT("ready");return true;
}

void ABridgeMobCharacter::SetAuthority(bool Active,ACharacter* NewTarget) {
    Enabled=Active && Alive();Target=NewTarget;
    auto* Movement=GetCharacterMovement();
    if(!Enabled) { Movement->StopMovementImmediately();Movement->SetMovementMode(MOVE_None); }
    else if(Movement->MovementMode==MOVE_None) Movement->SetMovementMode(MOVE_Walking);
}

void ABridgeMobCharacter::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    float Dt=FMath::Clamp(DeltaSeconds,0.f,.1f);
    HurtRemaining=float(FMath::Max(0.,LastFullHit+.5-GetWorld()->GetTimeSeconds()));
    for(auto* Part:Parts) if(auto* Material=Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0))) Material->SetScalarParameterValue(TEXT("BridgeHurt"),HurtRemaining>0 ? 1.f : 0.f);
    if(!Alive()) {
        DeathAge=float(FMath::Max(0.,GetWorld()->GetTimeSeconds()-DeathStarted));
        const float Angle=FMath::Min(90.f,FMath::Sqrt(FMath::Max(0.f,(DeathAge*20-1)/20*1.6f))*90.f);
        VisualRoot->SetRelativeLocation(DeathRootPosition);VisualRoot->SetRelativeRotation(FRotator(0,0,Angle));
        float Bottom=TNumericLimits<float>::Max();
        for(auto* Part:Parts) if(Part->GetNumSections()>0) Bottom=FMath::Min(Bottom,float(Part->CalcBounds(Part->GetComponentTransform()).GetBox().Min.Z));
        if(Bottom<TNumericLimits<float>::Max()) VisualRoot->AddWorldOffset(FVector(0,0,DeathFloorZ-Bottom));
        if(DeathAge>=1.f) Destroy();return;
    }
    if(!Enabled) return;
    AttackCooldown=FMath::Max(0.f,AttackCooldown-Dt);Decision-=Dt;
    ACharacter* Player=WorldOwner.IsValid() && WorldOwner->IsCreative() ? nullptr : Target.Get();FVector Direction=FVector::ZeroVector;
    if(Hostile && Player && (!WorldOwner.IsValid() || WorldOwner->PlayerHealth>0)) {
        FVector Offset=Player->GetActorLocation()-GetActorLocation();float Distance=Offset.Size2D();
        if(Distance<1600.f) {
            Direction=Offset.GetSafeNormal2D();
            const float Reach=GetCapsuleComponent()->GetScaledCapsuleRadius()+Player->GetSimpleCollisionRadius()+90.f;
            if(Distance<Reach && FMath::Abs(Offset.Z)<180.f) {
                Direction=FVector::ZeroVector;
                FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeMobMelee),false,this);
                bool Blocked=GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Player->GetActorLocation(),ECC_Visibility,Query);
                if(AttackCooldown<=0 && (!Blocked || Hit.GetActor()==Player)) {
                    if(WorldOwner.IsValid()) WorldOwner->HitPlayer(FMath::Max(1.f,Damage),GetActorLocation());
                    AttackCooldown=1.f;
                }
            }
        }
    }
    if(Direction.IsNearlyZero() && (!Hostile || !Player || FVector::DistSquared2D(Player->GetActorLocation(),GetActorLocation())>=1600.f*1600.f)) {
        if(Decision<=0) {
            Decision=Random.FRandRange(2.f,5.f);
            if(Random.FRand()<.45f) Wander=FVector::ZeroVector;
            else { float Angle=Random.FRandRange(-PI,PI);Wander=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0); }
        }
        Direction=Wander;
    }
    if(!Direction.IsNearlyZero()) {
        const FVector Feet=GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FVector NextFeet=Feet+Direction*GetCharacterMovement()->MaxWalkSpeed*Dt;
        if(WorldOwner.IsValid() && WorldOwner->SpawnAllowed && !WorldOwner->SpawnAllowed(NextFeet)) {
            GetCharacterMovement()->StopMovementImmediately();Wander=-Wander;Decision=.5f;Animate(Dt);return;
        }
        GetCharacterMovement()->MaxWalkSpeed=GroundSpeed*(Hostile && Player ? 1.f : .35f);
        AddMovementInput(Direction,1.f,true);
        if(FVector::DistSquared2D(GetActorLocation(),LastPosition)<1.f && GetCharacterMovement()->IsMovingOnGround()) Stuck+=Dt;else Stuck=0;
        if(Stuck>.35f) { Jump();Stuck=0;if(!Hostile) { Wander=-Wander;Decision=.5f; } }
    } else Stuck=0;
    Animate(Dt);
}

void ABridgeMobCharacter::Animate(float DeltaSeconds) {
    const float Distance=FVector::Dist2D(GetActorLocation(),LastPosition)/100.f;
    const float Weight=DeltaSeconds>0 ? FMath::Clamp(Distance/(DeltaSeconds*5.f),0.f,1.f) : 0;
    const double Decay=BridgeCombatMath::damping(.6,DeltaSeconds);
    const double Progress=Weight*DeltaSeconds*20+(WalkWeight-Weight)*.6*(1-Decay)/.4;
    WalkWeight=float(Weight+(WalkWeight-Weight)*Decay);
    Phase+=float(Progress)*(InitialSnapshot.Baby ? 3.f : 1.f)*.6662f/(2*PI)*16.f;
    for(int32 Index=0;Index<Parts.Num();Index++) {
        const FBridgeMobPart& Part=Poses[Index];FTransform Pose=Part.Rest;
        if(Part.WalkFrames.Num()>=2) {
            float Frame=FMath::Fmod(Phase,float(Part.WalkFrames.Num()));int32 First=FMath::FloorToInt(Frame),Next=(First+1)%Part.WalkFrames.Num();
            FTransform Walk;Walk.Blend(Part.WalkFrames[First],Part.WalkFrames[Next],Frame-First);Pose.Blend(Part.Rest,Walk,WalkWeight);
        }
        // Head independently follows the nearby UE player without changing the capsule/body yaw.
        if(Part.Name.Contains(TEXT("head"),ESearchCase::IgnoreCase) && Target.IsValid()) {
            FVector Direction=Target->GetActorLocation()-GetActorLocation();
            FRotator Look=Direction.Rotation();float Yaw=FMath::Clamp(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Look.Yaw),-55.f,55.f);
            NativeViewPitch=FMath::Clamp(-Look.Pitch,-45.f,45.f);
            Pose.SetRotation(FRotator(-NativeViewPitch,Yaw,0).Quaternion()*Pose.GetRotation());
        }
        Parts[Index]->SetRelativeTransform(Pose);
    }
    LastPosition=GetActorLocation();
}

bool ABridgeMobCharacter::Hit(float Amount,const FVector& Direction) {
    if(!Enabled || !Alive() || !FMath::IsFinite(Amount) || Amount<=0) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    const float Accepted=float(BridgeCombatMath::acceptedDamage(Amount,Now-LastFullHit,PreviousDamage));
    if(Accepted<=0) return false;
    const bool Full=Now-LastFullHit>=.5;
    if(Full) {LastFullHit=Now;HurtRemaining=.5f;}
    PreviousDamage=Amount;Health=FMath::Max(0.f,Health-Accepted);
    if(WorldOwner.IsValid()) WorldOwner->NotifyMobSound(MinecraftType,Alive() ? TEXT("hurt") : TEXT("death"),GetActorLocation());
    if(Alive() && Full && InitialSnapshot.KnockbackResistance<1) {
        const FVector Old=GetVelocity();const float Strength=800.f*(1.f-FMath::Clamp(InitialSnapshot.KnockbackResistance,0.f,1.f));
        const float Vertical=GetCharacterMovement()->IsMovingOnGround() ? FMath::Min(800.f,float(Old.Z)*.5f+Strength) : float(Old.Z);
        LaunchCharacter(Old*.5f+Direction.GetSafeNormal2D()*Strength+FVector(0,0,Vertical-Old.Z*.5f),true,true);
    } else if(!Alive()) {
        DeathStarted=Now;DeathRootPosition=VisualRoot->GetRelativeLocation();DeathFloorZ=GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(MobDeathFloor),false,this);
        if(GetWorld()->LineTraceSingleByChannel(Floor,GetActorLocation(),GetActorLocation()-FVector(0,0,1000),ECC_Visibility,Query)) DeathFloorZ=Floor.ImpactPoint.Z;
        GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetMovementMode(MOVE_None);GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    return true;
}

FBridgeMobSnapshot ABridgeMobCharacter::NativeSnapshot(const FVector& Anchor,const FVector& SourceOrigin) const {
    FBridgeMobSnapshot Snapshot=InitialSnapshot;
    const FVector Feet=GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Relative=(Feet-Anchor)/100.0;
    Snapshot.Position=SourceOrigin+FVector(-Relative.Y,Relative.Z,Relative.X);
    Snapshot.Id=MinecraftId;Snapshot.Type=MinecraftType;
    Snapshot.Yaw=GetActorRotation().Yaw;
    Snapshot.Health=Health;Snapshot.MaxHealth=MaxHealth;
    return Snapshot;
}
