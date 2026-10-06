#include "BridgeMobCharacter.h"
#include "BridgeMobWorld.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"

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
    if(!Appearance.Material || Appearance.Parts.IsEmpty() || Appearance.Parts.Num()>256) return false;
    MinecraftId=Snapshot.Id;MinecraftType=Snapshot.Type;Health=Snapshot.Health;MaxHealth=Snapshot.MaxHealth;
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
    Movement->MaxWalkSpeed=FMath::Clamp(Snapshot.Speed*1727.f,0.f,1200.f);
    Random.Initialize(int32(GetTypeHash(Snapshot.Id)));
    Poses=Appearance.Parts;
    for(int32 Index=0;Index<Poses.Num();Index++) {
        const FBridgeMobPart& Part=Poses[Index];
        if(Part.Parent>=Index || Part.Parent< -1 || Part.Vertices.Num()!=Part.Texcoords.Num() || Part.Vertices.Num()%4 || Part.Vertices.Num()>16384) return false;
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
        MobPartComponent->CreateMeshSection_LinearColor(0,Part.Vertices,Triangles,Normals,Part.Texcoords,Colors,Tangents,false);
        MobPartComponent->SetMaterial(0,Appearance.Material);
    }
    LastPosition=GetActorLocation();GetCharacterMovement()->SetMovementMode(MOVE_None);return true;
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
    if(!Alive()) {
        DeathAge+=Dt;VisualRoot->SetRelativeRotation(FRotator(0,0,FMath::Min(90.f,DeathAge*450.f)));
        if(DeathAge>.9f) Destroy();return;
    }
    if(!Enabled) return;
    AttackCooldown=FMath::Max(0.f,AttackCooldown-Dt);Decision-=Dt;
    ACharacter* Player=Target.Get();FVector Direction=FVector::ZeroVector;
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
        AddMovementInput(Direction,Hostile ? 1.f : .35f,true);
        if(FVector::DistSquared2D(GetActorLocation(),LastPosition)<1.f && GetCharacterMovement()->IsMovingOnGround()) Stuck+=Dt;else Stuck=0;
        if(Stuck>.35f) { Jump();Stuck=0;if(!Hostile) { Wander=-Wander;Decision=.5f; } }
    } else Stuck=0;
    LastPosition=GetActorLocation();Animate(Dt);
}

void ABridgeMobCharacter::Animate(float DeltaSeconds) {
    float Speed=GetVelocity().Size2D();float Weight=FMath::Clamp(Speed/260.f,0.f,1.f);
    WalkWeight=FMath::FInterpTo(WalkWeight,Weight,DeltaSeconds,10.f);Phase+=Speed*DeltaSeconds/230.f*16.f;
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
            Pose.SetRotation(FQuat(FVector::UpVector,FMath::DegreesToRadians(Yaw))*Pose.GetRotation());
        }
        Parts[Index]->SetRelativeTransform(Pose);
    }
}

bool ABridgeMobCharacter::Hit(float Amount,const FVector& Direction) {
    if(!Enabled || !Alive() || !FMath::IsFinite(Amount) || Amount<=0) return false;
    Health=FMath::Max(0.f,Health-Amount);
    if(WorldOwner.IsValid()) WorldOwner->NotifyMobSound(MinecraftType,Alive() ? TEXT("hurt") : TEXT("death"),GetActorLocation());
    if(Alive()) LaunchCharacter(Direction.GetSafeNormal2D()*220.f+FVector(0,0,180),true,true);
    else { GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetMovementMode(MOVE_None);GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
    return true;
}
