#include "BridgeMeshingMath.h"
#include "BridgeMobCharacter.h"
#include "BridgeMobWorld.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "BridgeCombatMath.h"
#include "BridgeMobAIMath.h"
#include "Materials/MaterialInstanceDynamic.h"

ABridgeMobCharacter::ABridgeMobCharacter() {
    PrimaryActorTick.bCanEverTick=true;
    bUseControllerRotationYaw=false;
    VisualRoot=CreateDefaultSubobject<USceneComponent>(TEXT("MinecraftMobModel"));VisualRoot->SetupAttachment(GetCapsuleComponent());
    GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    auto* Movement=GetCharacterMovement();
    Movement->bRunPhysicsWithNoController=true;Movement->bOrientRotationToMovement=false;
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
    ThrustSpeed=.2f/(Random.FRand()+1);
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
            // The reflected MC order already matches UE clockwise fronts.
            for(int32 TriangleIndex:BridgeMeshingMath::UEFrontQuad(Vertex)) Triangles.Add(TriangleIndex);
            FVector Normal=FVector::CrossProduct(Part.Vertices[Vertex+2]-Part.Vertices[Vertex],Part.Vertices[Vertex+1]-Part.Vertices[Vertex]).GetSafeNormal();
            for(int32 Offset=0;Offset<4;Offset++) Normals[Vertex+Offset]=Normal;
        }
        if(!Part.Vertices.IsEmpty()) MobPartComponent->CreateMeshSection_LinearColor(0,Part.Vertices,Triangles,Normals,Part.Texcoords,Colors,Tangents,false);
        MobPartComponent->SetMaterial(0,UMaterialInstanceDynamic::Create(Appearance.Material,MobPartComponent));
    }
    LastPosition=GetActorLocation();GetCharacterMovement()->SetMovementMode(MOVE_None);InitializationReason=TEXT("ready");return true;
}

void ABridgeMobCharacter::SetAuthority(bool Active,ACharacter* NewTarget) {
    // Receiver refreshes authority every frame. Do not clear a lethal impulse
    // or freeze gravity while the vanilla 20-tick death animation is running.
    if(!Alive()) {Target=NewTarget;return;}
    Enabled=Active && Alive();Target=NewTarget;
    auto* Movement=GetCharacterMovement();
    if(!Enabled) { Movement->StopMovementImmediately();Movement->SetMovementMode(MOVE_None); }
    else if(Movement->MovementMode==MOVE_None) {
        const auto Kind=BridgeMobAIMath::profile(TCHAR_TO_UTF8(*MinecraftType)).locomotion;
        const bool Aquatic=Kind==BridgeMobAIMath::Locomotion::Fish || Kind==BridgeMobAIMath::Locomotion::Squid;
        Movement->SetMovementMode(Kind==BridgeMobAIMath::Locomotion::Bat || Kind==BridgeMobAIMath::Locomotion::Flying || (Aquatic && InWater(GetActorLocation())) ? MOVE_Flying : MOVE_Walking);
        if(Movement->MovementMode==MOVE_Flying) {Movement->BrakingDecelerationFlying=0;Movement->BrakingFriction=0;Movement->bUseSeparateBrakingFriction=true;}
    }
}

void ABridgeMobCharacter::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if(!GetWorld()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    HurtRemaining=float(BridgeCombatMath::hurtTicks(Now-LastFullHit))*.05f;
    for(const auto& Part:Parts) if(auto* Material=Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0))) Material->SetScalarParameterValue(TEXT("BridgeHurt"),HurtRemaining>0 || !Alive() ? 1.f : 0.f);
    if(!Alive()) {
        DeathAge=float(FMath::Max(0.,Now-DeathStarted));
        // Retain lethal momentum; settle the rolled visual bounds at the feet
        // after landing so the imported model pivot cannot leave a floating corpse.
        VisualRoot->SetRelativeLocation(DeathRootPosition);
        VisualRoot->SetRelativeRotation(FRotator(0,0,float(BridgeCombatMath::deathRoll(DeathAge))));
        if(GetCharacterMovement()->IsMovingOnGround()) {
            FBox VisualBounds(ForceInit);
            for(const auto& Part:Parts) {Part->UpdateBounds();VisualBounds+=Part->Bounds.GetBox();}
            if(VisualBounds.IsValid) VisualRoot->AddWorldOffset(FVector(0,0,GetNativeFeet().Z+2-VisualBounds.Min.Z));
        }
        if(DeathAge>=1.f) {if(WorldOwner.IsValid() && WorldOwner->NativeDeathPoof) WorldOwner->NativeDeathPoof(this);Destroy();}return;
    }
    if(!Enabled) return;
    AiAccumulator+=FMath::Clamp(double(DeltaSeconds),0.,.25);
    while(AiAccumulator>=.05) {AiAccumulator-=.05;TickNativeAI();}
    // UE consumes movement input once per rendered frame; retain the 20 Hz
    // control intent between decisions instead of pulsing it at 20 FPS.
    const auto Locomotion=BridgeMobAIMath::profile(TCHAR_TO_UTF8(*MinecraftType)).locomotion;
    if(Now>=KnockbackUntil && GetCharacterMovement()->MovementMode!=MOVE_Flying && !MoveIntent.IsNearlyZero()) AddMovementInput(MoveIntent,1.f,true);
    const FVector Facing=GetVelocity().SizeSquared2D()>25 ? GetVelocity() : MoveIntent;
    if(!Facing.IsNearlyZero()) {
        const float MaxTurn=(Locomotion==BridgeMobAIMath::Locomotion::Bat ? 360.f : 180.f)*FMath::Max(0.f,DeltaSeconds);
        const float Yaw=GetActorRotation().Yaw;
        SetActorRotation(FRotator(0,Yaw+FMath::Clamp(FMath::FindDeltaAngleDegrees(Yaw,Facing.Rotation().Yaw),-MaxTurn,MaxTurn),0));
    }
    Animate(FMath::Max(0.f,DeltaSeconds));
}

bool ABridgeMobCharacter::InWater(const FVector& Position) const {
    return WorldOwner.IsValid() && WorldOwner->WaterAt && WorldOwner->WaterAt(Position);
}

bool ABridgeMobCharacter::ClearBody(const FVector& Feet) const {
    if(!GetWorld()) return false;
    if(WorldOwner.IsValid() && WorldOwner->SpawnAllowed && !WorldOwner->SpawnAllowed(Feet)) return false;
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MobPathBody),false,this);
    if(WorldOwner.IsValid()) Query.AddIgnoredActor(WorldOwner.Get());
    const auto* Capsule=GetCapsuleComponent();
    return !GetWorld()->OverlapAnyTestByObjectType(Feet+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()),FQuat::Identity,Objects,
        FCollisionShape::MakeCapsule(FMath::Max(1.f,Capsule->GetScaledCapsuleRadius()-.5f),FMath::Max(1.f,Capsule->GetScaledCapsuleHalfHeight()-.5f)),Query);
}

bool ABridgeMobCharacter::ProbeGround(const FVector& Seed,float PreviousZ,FVector& Feet) const {
    if(WorldOwner.IsValid() && WorldOwner->PrepareSpawnCollision) WorldOwner->PrepareSpawnCollision(Seed);
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MobPathFloor),false,this);FHitResult Floor;
    if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Seed.X,Seed.Y,PreviousZ+125),FVector(Seed.X,Seed.Y,PreviousZ-300),Objects,Query) || Floor.ImpactNormal.Z<.6f) return false;
    Feet=FVector(Seed.X,Seed.Y,Floor.ImpactPoint.Z+2);
    return Feet.Z-PreviousZ<=125 && ClearBody(Feet);
}

bool ABridgeMobCharacter::FindGroundWaypoint(const FVector& Destination,FVector& Next) {
    // Bounded voxel A*: ground support, headroom and one-block steps are
    // checked before a node enters the path. No jump-at-every-wall fallback.
    struct FNode {FIntPoint Key;FVector Feet;float Cost=0,Score=0;int32 Parent=-1;bool Closed=false;};
    const FVector Start=GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    TArray<FNode> Nodes;TMap<FIntPoint,int32> Indices;
    FNode Root;Root.Key=FIntPoint::ZeroValue;Root.Feet=Start;Root.Score=float(FVector::Dist2D(Start,Destination));Nodes.Add(Root);Indices.Add(Root.Key,0);
    int32 Best=0;
    const FIntPoint Steps[]={FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)};
    for(int32 Iteration=0;Iteration<64;++Iteration) {
        int32 Current=INDEX_NONE;float Score=TNumericLimits<float>::Max();
        for(int32 I=0;I<Nodes.Num();++I) if(!Nodes[I].Closed && Nodes[I].Score<Score) {Current=I;Score=Nodes[I].Score;}
        if(Current==INDEX_NONE) break;
        Nodes[Current].Closed=true;const FNode From=Nodes[Current];
        if(FVector::DistSquared2D(From.Feet,Destination)<FVector::DistSquared2D(Nodes[Best].Feet,Destination)) Best=Current;
        if(FVector::DistSquared2D(From.Feet,Destination)<10000) {Best=Current;break;}
        for(const FIntPoint& Step:Steps) {
            const FIntPoint Key=From.Key+Step;
            if(FMath::Abs(Key.X)>8 || FMath::Abs(Key.Y)>8 || Indices.Contains(Key)) continue;
            FVector Feet;const FVector Seed=Start+FVector(Key.X*100,Key.Y*100,From.Feet.Z-Start.Z);
            if(!ProbeGround(Seed,float(From.Feet.Z),Feet)) continue;
            // Reject routes through a wall above the proposed step. A 1-block
            // rise is crossed at jumping height; tall walls remain impassable.
            const float TravelZ=FMath::Max(float(From.Feet.Z),float(Feet.Z));
            FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(MobPathEdge),false,this);FHitResult Hit;
            const float Half=GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            if(GetWorld()->SweepSingleByObjectType(Hit,FVector(From.Feet.X,From.Feet.Y,TravelZ+Half),FVector(Feet.X,Feet.Y,TravelZ+Half),FQuat::Identity,Objects,
                FCollisionShape::MakeCapsule(FMath::Max(1.f,GetCapsuleComponent()->GetScaledCapsuleRadius()-.5f),FMath::Max(1.f,Half-.5f)),Query)) continue;
            FNode Node;Node.Key=Key;Node.Feet=Feet;Node.Parent=Current;Node.Cost=From.Cost+100+FMath::Abs(float(Feet.Z-From.Feet.Z))*.5f;
            Node.Score=Node.Cost+float(FVector::Dist2D(Feet,Destination));Indices.Add(Key,Nodes.Num());Nodes.Add(Node);
        }
    }
    if(Best==0) return false;
    while(Nodes[Best].Parent>0) Best=Nodes[Best].Parent;
    Next=Nodes[Best].Feet;return true;
}

void ABridgeMobCharacter::TickNativeAI() {
    if(GetWorld()->GetTimeSeconds()<KnockbackUntil) {MoveIntent=FVector::ZeroVector;return;}
    ++AiTicks;AttackTicks=FMath::Max(0,AttackTicks-1);JumpTicks=FMath::Max(0,JumpTicks-1);PathTicks=FMath::Max(0,PathTicks-1);PanicTicks=FMath::Max(0,PanicTicks-1);
    StopJumping();
    MoveIntent=FVector::ZeroVector;
    const auto Profile=BridgeMobAIMath::profile(TCHAR_TO_UTF8(*MinecraftType));
    auto* Movement=GetCharacterMovement();
    const FVector Feet=GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    ACharacter* Player=WorldOwner.IsValid() && (WorldOwner->IsCreative() || WorldOwner->PlayerHealth<=0) ? nullptr : Target.Get();
    if(Profile.locomotion==BridgeMobAIMath::Locomotion::Bat) {
        Movement->SetMovementMode(MOVE_Flying);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MobBatRoost),false,this);FHitResult Ceiling;
        const FVector Top=GetActorLocation()+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const bool Roof=GetWorld()->LineTraceSingleByChannel(Ceiling,Top,Top+FVector(0,0,5),ECC_Visibility,Query) && Ceiling.ImpactNormal.Z<-.5f;
        if(AiTicks==1) BatRoosting=Roof;
        if(BatRoosting && (!Roof || (Target.IsValid() && FVector::DistSquared(Target->GetActorLocation(),GetActorLocation())<160000))) BatRoosting=false;
        if(BatRoosting) {Movement->Velocity=FVector::ZeroVector;return;}
        if(!HasGoal || Random.RandRange(0,29)==0 || FVector::DistSquared(Feet,GoalPosition)<40000 || !ClearBody(GoalPosition)) {
            HasGoal=false;
            for(int32 Try=0;Try<10;++Try) {
                const FVector Candidate=Feet+FVector((Random.RandRange(0,6)-Random.RandRange(0,6))*100,(Random.RandRange(0,6)-Random.RandRange(0,6))*100,(Random.RandRange(0,5)-2)*100);
                if(ClearBody(Candidate)) {GoalPosition=Candidate;HasGoal=true;break;}
            }
        }
        const FVector Offset=HasGoal ? GoalPosition-Feet : FVector::ZeroVector;
        const FVector Old=Movement->Velocity/2000.;
        Movement->Velocity=FVector(BridgeMobAIMath::batComponent(Old.X,Offset.X,false),BridgeMobAIMath::batComponent(Old.Y,Offset.Y,false),BridgeMobAIMath::batComponent(Old.Z,Offset.Z,true)*.6)*2000.;
        MoveIntent=Movement->Velocity.GetSafeNormal();
        if(Roof && Random.RandRange(0,99)==0) BatRoosting=true;
        return;
    }
    const bool Aquatic=Profile.locomotion==BridgeMobAIMath::Locomotion::Fish || Profile.locomotion==BridgeMobAIMath::Locomotion::Squid;
    if(Profile.locomotion==BridgeMobAIMath::Locomotion::Squid && InWater(GetActorLocation())) {
        Movement->SetMovementMode(MOVE_Flying);
        // SquidEntity.SwimGoal changes the direction on average every 50
        // entity ticks. Its thrust cycle moves in pulses, not fish steering.
        if(SwimVector.IsNearlyZero() || (AiTicks%2==0 && Random.RandRange(0,24)==0)) {
            const float Angle=Random.FRand()*2*PI;
            SwimVector=FVector(FMath::Cos(Angle)*400,FMath::Sin(Angle)*400,Random.FRandRange(-200,200));
        }
        ThrustTimer+=ThrustSpeed;
        if(ThrustTimer>2*PI) {ThrustTimer-=2*PI;if(Random.RandRange(0,9)==0) ThrustSpeed=.2f/(Random.FRand()+1);}
        if(ThrustTimer<PI && ThrustTimer/PI>.75f) {
            const FVector Ahead=Feet+SwimVector*.1;
            if(ClearBody(Ahead) && InWater(Ahead+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))) Movement->Velocity=SwimVector;
            else {SwimVector=-SwimVector;Movement->Velocity=FVector::ZeroVector;}
        } else if(ThrustTimer>=PI) Movement->Velocity*=.9;
        MoveIntent=Movement->Velocity.GetSafeNormal();return;
    }
    if(Aquatic && InWater(GetActorLocation())) {
        Movement->SetMovementMode(MOVE_Flying);
        if(!HasGoal || FVector::DistSquared(Feet,GoalPosition)<10000 || !InWater(GoalPosition+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))) {
            HasGoal=false;
            for(int32 Try=0;Try<10;++Try) {
                const FVector Candidate=Feet+FVector(Random.RandRange(-5,5)*100,Random.RandRange(-5,5)*100,Random.RandRange(-2,2)*100);
                if(ClearBody(Candidate) && InWater(Candidate+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))) {GoalPosition=Candidate;HasGoal=true;break;}
            }
        }
        FVector Direction=HasGoal ? (GoalPosition-Feet).GetSafeNormal() : FVector::ZeroVector;float SpeedMultiplier=1;
        if(Profile.locomotion==BridgeMobAIMath::Locomotion::Fish && Target.IsValid() && FVector::DistSquared(Target->GetActorLocation(),GetActorLocation())<640000) {
            Direction=(GetActorLocation()-Target->GetActorLocation()).GetSafeNormal();
            SpeedMultiplier=FVector::DistSquared(Target->GetActorLocation(),GetActorLocation())<490000 ? 1.4f : 1.6f;
        }
        FishSpeed=float(BridgeMobAIMath::fishSpeed(FishSpeed,InitialSnapshot.Speed,SpeedMultiplier));
        // FishMoveControl buoyancy +.005 cancels travel's -.005. Input adds
        // .01 blocks/tick, travel moves then damps all axes by .9.
        FVector Velocity=Movement->Velocity+Direction.GetSafeNormal2D()*FishSpeed*19.6f;
        Velocity.Z+=Direction.Z*FishSpeed*200;
        if(!InWater(Feet+FVector(0,0,InitialSnapshot.Height*100-.1f))) Velocity.Z-=10;
        Movement->Velocity=Velocity*.9;
        MoveIntent=Direction;return;
    }
    if(Profile.locomotion==BridgeMobAIMath::Locomotion::Flying) {
        // Specialized flying goals are not replaced by ground jumping. Keep
        // captured copies aloft; species-specific attack/navigation is pending.
        Movement->SetMovementMode(MOVE_Flying);Movement->Velocity*=.91;return;
    }
    if(!Aquatic && InWater(Feet+FVector(0,0,InitialSnapshot.Height<.5f ? 5.f : 40.f))) {
        // SwimGoal owns JUMP independently of wander/chase's MOVE control.
        Movement->SetMovementMode(MOVE_Flying);
        FVector Direction=Hostile && Player ? (Player->GetActorLocation()-GetActorLocation()).GetSafeNormal2D()
            : HasGoal ? (GoalPosition-Feet).GetSafeNormal2D() : FVector::ZeroVector;
        FVector Velocity=Movement->Velocity+Direction*InitialSnapshot.Speed*39.2f;
        if(Random.FRand()<.8f) Velocity.Z+=40;Velocity.Z-=10;
        Movement->Velocity=Velocity*.8f;return;
    }
    if(Movement->MovementMode==MOVE_Flying) {Movement->bUseSeparateBrakingFriction=false;Movement->SetMovementMode(MOVE_Falling);}
    if(Aquatic && Movement->IsMovingOnGround() && JumpTicks==0) {
        Movement->Velocity+=FVector(Random.FRandRange(-100,100),Random.FRandRange(-100,100),800);Movement->SetMovementMode(MOVE_Falling);JumpTicks=10;return;
    }
    bool Chasing=Hostile && Player && FVector::DistSquared2D(Player->GetActorLocation(),GetActorLocation())<FMath::Square(Profile.followRange*100);
    if(PanicTicks>0 && !Hostile) {
        if(HasGoal && FVector::DistSquared2D(Feet,GoalPosition)<2500) {HasGoal=false;PanicTicks=0;}
        if(!HasGoal) for(int32 Try=0;Try<10;++Try) {
            FVector Candidate;const FVector Seed=Feet+FVector(Random.RandRange(-5,5)*100,Random.RandRange(-5,5)*100,0);
            if(ProbeGround(Seed,float(Feet.Z),Candidate)) {GoalPosition=Candidate;HasGoal=true;GoalSpeed=float(Profile.panicSpeed);break;}
        }
    }
    else if(Chasing) {
        GoalPosition=Player->GetActorLocation()-FVector(0,0,Player->GetSimpleCollisionHalfHeight());HasGoal=true;GoalSpeed=float(Profile.chaseSpeed);
        const FVector Delta=Player->GetActorLocation()-GetActorLocation();
        const float Range=float(BridgeMobAIMath::attackRangeBlocks()*100);
        const bool InReach=FMath::Abs(Delta.X)<GetCapsuleComponent()->GetScaledCapsuleRadius()+Player->GetSimpleCollisionRadius()+Range
            && FMath::Abs(Delta.Y)<GetCapsuleComponent()->GetScaledCapsuleRadius()+Player->GetSimpleCollisionRadius()+Range
            && FMath::Abs(Delta.Z)<GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+Player->GetSimpleCollisionHalfHeight();
        if(InReach) {
            FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeMobMelee),false,this);
            const bool Blocked=GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Player->GetActorLocation(),ECC_Visibility,Query);
            if(!Blocked || Hit.GetActor()==Player) {
                if(AttackTicks==0 && Damage>0 && WorldOwner.IsValid()) {WorldOwner->HitPlayer(Damage,GetActorLocation());AttackTicks=20;}
                HasWaypoint=false;return;
            }
        }
    } else {
        if(HasGoal && FVector::DistSquared2D(Feet,GoalPosition)<2500) {HasGoal=false;HasWaypoint=false;}
        if(!HasGoal && AiTicks%2==0 && Random.RandRange(0,BridgeMobAIMath::goalChance(Profile.wanderChance)-1)==0) {
            for(int32 Try=0;Try<10;++Try) {
                FVector Candidate;const FVector Seed=Feet+FVector(Random.RandRange(-10,10)*100,Random.RandRange(-10,10)*100,0);
                if(ProbeGround(Seed,float(Feet.Z),Candidate)) {GoalPosition=Candidate;HasGoal=true;GoalSpeed=float(Profile.wanderSpeed);break;}
            }
        }
    }
    if(!HasGoal) {HasWaypoint=false;LastWaypointDistance=-1;StalledPathTicks=0;return;}
    if(HasWaypoint) {
        const float Distance=float(FVector::Dist2D(Feet,Waypoint));
        StalledPathTicks=LastWaypointDistance>=0 && Distance>LastWaypointDistance-1 ? StalledPathTicks+1 : 0;
        LastWaypointDistance=Distance;
        if(StalledPathTicks>=10) {HasWaypoint=false;StalledPathTicks=0;LastWaypointDistance=-1;}
    }
    if(PathTicks==0 || !HasWaypoint || FVector::DistSquared2D(Feet,Waypoint)<900) {
        // Do not re-anchor a valid waypoint grid every few AI ticks: that
        // oscillates between axis directions before the current step is reached.
        if(!HasWaypoint || FVector::DistSquared2D(Feet,Waypoint)<900) {HasWaypoint=FindGroundWaypoint(GoalPosition,Waypoint);LastWaypointDistance=-1;}
        PathTicks=Chasing ? 4+Random.RandRange(0,6) : 10;
        if(!HasWaypoint) {++BlockedTicks;if(!Chasing && BlockedTicks>3) HasGoal=false;return;}
        BlockedTicks=0;
    }
    const FVector Offset=Waypoint-Feet;const FVector Direction=Offset.GetSafeNormal2D();
    Movement->MaxWalkSpeed=GroundSpeed*GoalSpeed;Movement->MaxAcceleration=GroundSpeed*GoalSpeed*12.1f;

    MoveIntent=Direction;
    const float Rise=float(Offset.Z/100),Distance=float(Offset.SizeSquared2D()/10000);
    if(BridgeMobAIMath::canJump(Rise,Distance,InitialSnapshot.Width,Movement->IsMovingOnGround(),ClearBody(Feet+FVector(0,0,FMath::Max(0.f,float(Offset.Z)))),false,JumpTicks)) {Jump();JumpTicks=10;}
}

void ABridgeMobCharacter::Animate(float DeltaSeconds) {
    const float Distance=FVector::Dist2D(GetActorLocation(),LastPosition)/100.f;
    const bool Bat=MinecraftType==TEXT("minecraft:bat");
    const float Weight=Bat ? (BatRoosting ? 0.f : 1.f) : DeltaSeconds>0 ? FMath::Clamp(Distance/(DeltaSeconds*5.f),0.f,1.f) : 0;
    const double Decay=BridgeCombatMath::damping(.6,DeltaSeconds);
    const double Progress=Weight*DeltaSeconds*20+(WalkWeight-Weight)*.6*(1-Decay)/.4;
    WalkWeight=float(Weight+(WalkWeight-Weight)*Decay);
    Phase+=float(Progress)*(InitialSnapshot.Baby ? 3.f : 1.f)*.6662f/(2*PI)*16.f;
    if(Bat) {WalkWeight=Weight;Phase=float(GetWorld()->GetTimeSeconds())*32.f;}
    for(int32 Index=0;Index<Parts.Num();Index++) {
        const FBridgeMobPart& Part=Poses[Index];FTransform Pose=Part.Rest;
        if(Part.WalkFrames.Num()>=2) {
            float Frame=FMath::Fmod(Phase,float(Part.WalkFrames.Num()));int32 First=FMath::FloorToInt(Frame),Next=(First+1)%Part.WalkFrames.Num();
            FTransform Walk;Walk.Blend(Part.WalkFrames[First],Part.WalkFrames[Next],Frame-First);Pose.Blend(Part.Rest,Walk,WalkWeight);
        }
        // Head independently follows the nearby UE player without changing the capsule/body yaw.
        if(Part.Name.Contains(TEXT("head"),ESearchCase::IgnoreCase) && Target.IsValid() && FVector::DistSquared(Target->GetActorLocation(),GetActorLocation())<FMath::Square(Hostile ? 800.f : 600.f)) {
            FVector Direction=Target->GetActorLocation()-GetActorLocation();
            FRotator Look=Direction.Rotation();float Yaw=FMath::Clamp(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Look.Yaw),-55.f,55.f);
            NativeViewPitch=FMath::FInterpTo(NativeViewPitch,FMath::Clamp(-Look.Pitch,-45.f,45.f),DeltaSeconds,8.f);
            HeadYaw=FMath::FInterpTo(HeadYaw,Yaw,DeltaSeconds,8.f);
            Pose.SetRotation(FRotator(-NativeViewPitch,HeadYaw,0).Quaternion()*Pose.GetRotation());
        }
        Parts[Index]->SetRelativeTransform(Pose);
    }
    LastPosition=GetActorLocation();
}

void ABridgeMobCharacter::ApplyNativeKnockback(float StrengthBlocksPerTick,const FVector& AwayDirection) {
    if(!Enabled || !GetWorld() || !FMath::IsFinite(StrengthBlocksPerTick) || StrengthBlocksPerTick<=0) return;
    auto* Movement=GetCharacterMovement();const FVector Old=Movement->Velocity;
    FVector Away=AwayDirection;
    if(Away.SizeSquared2D()<.1) do {Away.X=Random.FRand()-Random.FRand();Away.Y=Random.FRand()-Random.FRand();} while(Away.SizeSquared2D()<.1);
    const auto Result=BridgeCombatMath::knockbackVelocity({Old.X,Old.Y,Old.Z},StrengthBlocksPerTick,InitialSnapshot.KnockbackResistance,Away.X,Away.Y,Movement->IsMovingOnGround());
    // LaunchCharacter defers the impulse until CharacterMovement consumes it;
    // writing Velocity on a walking frame can be projected away by floor physics.
    LaunchCharacter(FVector(Result.x,Result.y,Result.z),true,true);
    KnockbackUntil=GetWorld()->GetTimeSeconds()+.25;
    MoveIntent=FVector::ZeroVector;HasWaypoint=false;PathTicks=0;
}

bool ABridgeMobCharacter::Hit(float Amount,const FVector& Direction) {
    if(!Enabled || !Alive() || !GetWorld() || !FMath::IsFinite(Amount) || Amount<=0) return false;
    const double Now=GetWorld()->GetTimeSeconds();
    const float Accepted=float(BridgeCombatMath::acceptedDamage(Amount,Now-LastFullHit,PreviousDamage));
    if(Accepted<=0) return false;
    const bool Full=BridgeCombatMath::fullHit(Now-LastFullHit);
    if(Full) {LastFullHit=Now;HurtRemaining=.5f;}
    PreviousDamage=Amount;Health=FMath::Max(0.f,Health-float(BridgeCombatMath::armorDamage(Accepted,InitialSnapshot.Armor,InitialSnapshot.ArmorToughness)));
    if(Full && WorldOwner.IsValid()) WorldOwner->NotifyMobSound(MinecraftType,Alive() ? TEXT("hurt") : TEXT("death"),GetActorLocation(),InitialSnapshot.Baby,InitialSnapshot.Width/.52f);
    // LivingEntity.damage applies the base full-hit impulse before onDeath,
    // including lethal hits. Stronger excess damage does not repeat it.
    if(Full) ApplyNativeKnockback(.4f,Direction);
    if(!Hostile) {PanicTicks=40;PathTicks=0;HasGoal=false;}
    BatRoosting=false;
    if(!Alive()) {
        DeathStarted=Now;DeathRootPosition=VisualRoot->GetRelativeLocation();MoveIntent=FVector::ZeroVector;
        // Keep terrain collision/gravity for the 20 death ticks, while making
        // the corpse untargetable and nonblocking to other entities.
        GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
        GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        if(GetCharacterMovement()->MovementMode==MOVE_Flying) GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }
    return true;
}

FVector ABridgeMobCharacter::GetNativeFeet() const {
    return GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
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
