#include "BridgeVanillaEffects.h"
#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "BridgeCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

ABridgeVanillaEffects::ABridgeVanillaEffects() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(PlaneAsset.Succeeded()) ParticlePlane=PlaneAsset.Object;
}
void ABridgeVanillaEffects::Configure(ABridgeWorld* ImportedTerrain,UBridgeBlockPalette* ImportedPalette,UMaterialInterface* Material) {
    if(Terrain!=ImportedTerrain || Palette!=ImportedPalette || DustMaterial!=Material) ClearParticles();
    Terrain=ImportedTerrain; Palette=ImportedPalette; DustMaterial=Material;
}
void ABridgeVanillaEffects::SetViewCamera(UCameraComponent* Camera) { ViewCamera=Camera; }
void ABridgeVanillaEffects::ResetMovement() {
    HaveMovementSample=false; SampledCharacter.Reset(); WalkDistance=0; SprintClock=0;
}
void ABridgeVanillaEffects::ClearParticles() {
    Particles.Empty(); PhysicsClock=0;
    for(auto& Entry:Groups) if(Entry.Value) Entry.Value->DestroyComponent();
    Groups.Empty();
}
void ABridgeVanillaEffects::EndPlay(const EEndPlayReason::Type Reason) { ClearParticles(); Super::EndPlay(Reason); }

FString ABridgeVanillaEffects::FindGroup(const FString& BlockId,FColor Tint) {
    if(!ParticlePlane || !Palette || !DustMaterial) return FString();
    UTexture* DustTexture=Palette->FindParticleTexture(BlockId);
    if(DustTexture) Tint=Palette->ParticleTint(BlockId);
    else {
        UMaterialInterface* BlockMaterial=Palette->Find(BlockId); if(!BlockMaterial) return FString();
        float TintEnabled=0;
        const bool Grass=BlockId==TEXT("minecraft:grass_block");
        const FName TextureParameter(Grass ? TEXT("BottomTexture") : TEXT("SideTexture"));
        if(!BlockMaterial->GetTextureParameterValue(FMaterialParameterInfo(TextureParameter),DustTexture) || !DustTexture) return FString();
        if(!Grass) BlockMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SideTint")),TintEnabled);
        if(TintEnabled<.5f || Grass) Tint=FColor::White;
    }
    if(BlockId==TEXT("minecraft:grass_block")) Tint=FColor::White;
    const FString Key=FString::Printf(TEXT("%s#%u"),*DustTexture->GetPathName(),Tint.ToPackedARGB());
    if(Groups.Contains(Key)) return Key;
    if(Groups.Num()>=64) return FString(); // Bound resource costs when many different blocks are broken.
    auto* Group=NewObject<UInstancedStaticMeshComponent>(this);
    Group->SetupAttachment(RootComponent); Group->SetMobility(EComponentMobility::Movable);
    Group->SetStaticMesh(ParticlePlane); Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Group->SetCanEverAffectNavigation(false); Group->SetGenerateOverlapEvents(false); Group->SetCastShadow(false);
    Group->NumCustomDataFloats=2;
    auto* Dynamic=UMaterialInstanceDynamic::Create(DustMaterial,this);
    Dynamic->SetTextureParameterValue(TEXT("ParticleTexture"),DustTexture);
    Dynamic->SetVectorParameterValue(TEXT("ParticleColor"),FLinearColor::FromSRGBColor(Tint)*.6f);
    Group->SetMaterial(0,Dynamic); Group->RegisterComponent(); Groups.Add(Key,Group);
    return Key;
}
void ABridgeVanillaEffects::AddParticle(const FVector& Position,const FVector& Velocity,const FString& Group) {
    if(Group.IsEmpty() || Particles.Num()>=384) return;
    FDustParticle Particle;
    Particle.Group=Group; Particle.Position=Position; Particle.Previous=Position;
    // Minecraft Particle adds uniformly random velocity, normalizes it, then adds upward motion.
    FVector RandomDirection=Velocity+FVector(FMath::FRandRange(-.4f,.4f),FMath::FRandRange(-.4f,.4f),FMath::FRandRange(-.4f,.4f));
    Particle.Velocity=RandomDirection.GetSafeNormal()*(FMath::FRand()+FMath::FRand()+1)*120+FVector(0,0,200);
    Particle.Size=FMath::FRandRange(10,20);
    Particle.Lifetime=FMath::FloorToInt(4/(FMath::FRand()*.9f+.1f));
    Particle.TextureOffset=FVector2D(FMath::FRand()*.75f,FMath::FRand()*.75f);
    Particles.Add(MoveTemp(Particle));
}
void ABridgeVanillaEffects::SpawnBreak(const FVector& Center,const FString& BlockId,FColor Tint) {
    const FString Group=FindGroup(BlockId,Tint); if(Group.IsEmpty()) return;
    // A full block uses the same four subdivisions per axis as the vanilla break effect.
    for(int32 X=0;X<4;++X) for(int32 Y=0;Y<4;++Y) for(int32 Z=0;Z<4;++Z) {
        const FVector Offset((X+.5f)*25-50,(Y+.5f)*25-50,(Z+.5f)*25-50);
        AddParticle(Center+Offset,Offset*.01f,Group);
    }
}
void ABridgeVanillaEffects::SpawnSprint(const FVector& Feet,const FVector& Velocity,const FString& BlockId,FColor Tint) {
    const FString Group=FindGroup(BlockId,Tint); if(Group.IsEmpty()) return;
    // Entity's -4*motion / 1.5 upward inputs still pass through Particle's random normalization.
    const FVector SeedVelocity(-4*Velocity.X/2000,-4*Velocity.Y/2000,1.5f);
    AddParticle(Feet+FVector(FMath::FRandRange(-30,30),FMath::FRandRange(-30,30),10),SeedVelocity,Group);
}
void ABridgeVanillaEffects::SampleCharacter(ABridgeCharacter* Character,float DeltaSeconds,TArray<FBridgeVanillaEvent>& OutEvents) {
    if(!IsValid(Character) || !Character->UEAuthority || !Terrain || !Terrain->IsSealed()) { ResetMovement(); return; }
    const auto* Movement=Character->GetCharacterMovement(); const auto* Capsule=Character->GetCapsuleComponent();
    if(!Movement || !Capsule) { ResetMovement(); return; }
    const FVector Feet=Character->GetActorLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
    const bool Grounded=Movement->IsMovingOnGround();
    if(!HaveMovementSample || SampledCharacter.Get()!=Character || FVector::DistSquared(Feet,PreviousFeet)>40000) {
        SampledCharacter=Character; HaveMovementSample=true; PreviousFeet=Feet; WasGrounded=Grounded;
        FallPeak=Feet.Z; WalkDistance=0; SprintClock=0; return;
    }
    FBridgeVanillaEvent Surface;
    const bool HasSurface=Grounded && Terrain->GetSupportingBlock(Feet,Surface.SourceVoxel,Surface.BlockId,Surface.Tint,Surface.Position,Character);
    if(!Grounded) FallPeak=FMath::Max(FallPeak,static_cast<float>(Feet.Z));
    if(Grounded && !WasGrounded && HasSurface) {
        const float Distance=FMath::Max(0.f,(FallPeak-static_cast<float>(Feet.Z))*.01f);
        if(Distance>=.25f) { auto Event=Surface; Event.Type=TEXT("land"); Event.FallDistance=Distance; OutEvents.Add(MoveTemp(Event)); }
        WalkDistance=0;
    }
    if(Grounded) FallPeak=Feet.Z;
    const FVector Velocity=Character->GetVelocity();
    const float HorizontalDistance=FVector::Dist2D(Feet,PreviousFeet);
    if(HasSurface && !Character->bIsCrouched && HorizontalDistance>.01f && Velocity.SizeSquared2D()>25) {
        WalkDistance+=HorizontalDistance;
        // Entity.distanceTraveled adds 0.6*distance and advances one integer for each step.
        if(WalkDistance>=100/.6f) { WalkDistance=FMath::Fmod(WalkDistance,100/.6f); auto Event=Surface; Event.Type=TEXT("step"); OutEvents.Add(MoveTemp(Event)); }
        if(Movement->MaxWalkSpeed>500) {
            SprintClock+=FMath::Clamp(DeltaSeconds,0.f,.1f);
            if(SprintClock>=.05f) { SprintClock=FMath::Fmod(SprintClock,.05f); SpawnSprint(Surface.Position,Velocity,Surface.BlockId,Surface.Tint); }
        } else SprintClock=0;
    } else SprintClock=0;
    PreviousFeet=Feet; WasGrounded=Grounded;
}
void ABridgeVanillaEffects::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if(Particles.IsEmpty()) return;
    PhysicsClock+=FMath::Clamp(DeltaSeconds,0.f,.1f);
    while(PhysicsClock>=.05f) {
        PhysicsClock-=.05f;
        for(auto& Particle:Particles) {
            Particle.Previous=Particle.Position;
            if(Particle.Age++>=Particle.Lifetime) continue;
            Particle.Velocity.Z-=80; // 0.04 Minecraft blocks/tick gravity at 20 Hz.
            FVector Next=Particle.Position+Particle.Velocity*.05f;
            FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgeDust),false);
            if(SampledCharacter.IsValid()) Params.AddIgnoredActor(SampledCharacter.Get());
            if(GetWorld()->SweepSingleByChannel(Hit,Particle.Position,Next,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(2),Params)) {
                Next=Hit.Location+Hit.Normal*.1f;
                Particle.Velocity=FVector::VectorPlaneProject(Particle.Velocity,Hit.Normal);
                if(Hit.Normal.Z>.5f) { Particle.Velocity.X*=.7f; Particle.Velocity.Y*=.7f; }
            }
            Particle.Position=Next; Particle.Velocity*=.98f;
        }
        Particles.RemoveAll([](const FDustParticle& Particle){return Particle.Age>Particle.Lifetime;});
    }
    for(auto& Entry:Groups) if(Entry.Value) Entry.Value->ClearInstances();
    FQuat Facing=FQuat::Identity;
    if(ViewCamera.IsValid()) {
        const FQuat CameraRotation=ViewCamera->GetComponentQuat();
        Facing=FRotationMatrix::MakeFromXY(CameraRotation.RotateVector(FVector(0,1,0)),CameraRotation.RotateVector(FVector(0,0,1))).ToQuat();
    }
    const float Interpolation=PhysicsClock/.05f;
    TSet<FString> ActiveGroups;
    for(const auto& Particle:Particles) {
        auto* Found=Groups.Find(Particle.Group); if(!Found || !IsValid(*Found)) continue;
        auto* Group=Found->Get();
        const FVector RenderPosition=FMath::Lerp(Particle.Previous,Particle.Position,Interpolation);
        const int32 Instance=Group->AddInstance(FTransform(Facing,RenderPosition,FVector(Particle.Size*.01f)),true);
        Group->SetCustomDataValue(Instance,0,Particle.TextureOffset.X,false);
        Group->SetCustomDataValue(Instance,1,Particle.TextureOffset.Y,false);
        ActiveGroups.Add(Particle.Group);
    }
    for(auto& Entry:Groups) {
        if(ActiveGroups.Contains(Entry.Key)) Entry.Value->MarkRenderStateDirty();
    }
    for(auto It=Groups.CreateIterator();It;++It) if(!ActiveGroups.Contains(It.Key())) {
        if(It.Value()) It.Value()->DestroyComponent(); It.RemoveCurrent();
    }
}
