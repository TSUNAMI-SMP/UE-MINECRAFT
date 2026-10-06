#include "BridgeVanillaEffects.h"
#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "BridgeCharacter.h"
#include "BridgeParticleMath.h"
#include "BridgeProtocol.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

namespace {
// IEEE-754 JSON numbers are exact only through 2^53-1. Saturate long-lived counters.
constexpr uint64 MaxDiagnosticCount=9007199254740991ull;
void CountUp(uint64& Counter,uint64 Amount) { Counter=FMath::Min(MaxDiagnosticCount,Counter+Amount); }
}

ABridgeVanillaEffects::ABridgeVanillaEffects() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(PlaneAsset.Succeeded()) ParticlePlane=PlaneAsset.Object;
}
void ABridgeVanillaEffects::Configure(ABridgeWorld* ImportedTerrain,UBridgeBlockPalette* ImportedPalette,UMaterialInterface* Material) {
    const bool Changed=!Configured || Terrain!=ImportedTerrain || Palette!=ImportedPalette || DustMaterial!=Material;
    if(!Changed) return;
    ClearParticles();
    Terrain=ImportedTerrain; Palette=ImportedPalette; DustMaterial=Material;
    Configured=true; Diagnostics.MaterialReady=false; Diagnostics.TextureCount=0;
    UTexture* DefaultTexture=nullptr; FLinearColor DefaultColor;
    if(!IsValid(DustMaterial)) Diagnostics.Reason=TEXT("missing_material");
    else if(!DustMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("ParticleTexture")),DefaultTexture)
        || !IsValid(DefaultTexture) || !DustMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ParticleColor")),DefaultColor))
        Diagnostics.Reason=TEXT("material_parameters");
    else if(!DustMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes)) Diagnostics.Reason=TEXT("material_instancing");
    else { Diagnostics.MaterialReady=true; Diagnostics.Reason=TEXT("ready"); }
    TSet<UTexture*> Textures;
    if(IsValid(Palette)) {
        for(const auto& Entry:Palette->ParticleTextures) if(IsValid(Entry.Value)) Textures.Add(Entry.Value.Get());
        // A 0.6 export has no particle sprite map; retain its supported face-texture fallback.
        for(const auto& Entry:Palette->Materials) {
            UTexture* Texture=nullptr; FColor Tint=FColor::White;
            if(ResolveTexture(Entry.Key,Tint,Texture)) Textures.Add(Texture);
        }
    }
    Diagnostics.TextureCount=Textures.Num();
    if(Diagnostics.MaterialReady) {
        if(!IsValid(ParticlePlane)) Diagnostics.Reason=TEXT("missing_plane");
        else if(!IsValid(Palette)) Diagnostics.Reason=TEXT("missing_palette");
        else if(Textures.IsEmpty()) Diagnostics.Reason=TEXT("missing_texture");
    }
}
void ABridgeVanillaEffects::SetViewCamera(UCameraComponent* Camera) { ViewCamera=Camera; }
FBridgeDustDiagnostics ABridgeVanillaEffects::GetDiagnostics() const {
    auto Result=Diagnostics; Result.Active=Particles.Num(); Result.Groups=Groups.Num(); Result.Instances=0;
    Result.SizeMultiplier=BridgeParticleMath::SizeMultiplier(ParticleSizeMultiplier);
    Result.DensityMultiplier=BridgeParticleMath::DensityMultiplier(ParticleDensityMultiplier);
    Result.LifetimeMultiplier=BridgeParticleMath::LifetimeMultiplier(ParticleLifetimeMultiplier);
    for(const auto& Entry:Groups) if(IsValid(Entry.Value)) Result.Instances+=Entry.Value->GetInstanceCount();
    if(Result.Reason==TEXT("ready") && !ViewCamera.IsValid()) Result.Reason=TEXT("missing_camera");
    return Result;
}
void ABridgeVanillaEffects::ResetMovement() {
    HaveMovementSample=false; SampledCharacter.Reset(); WalkDistance=0; SprintClock=0;SprintDensityAccumulator=0;
}
void ABridgeVanillaEffects::ConfigureParticleTuning(float SizeMultiplier,float DensityMultiplier,float LifetimeMultiplier) {
    ParticleSizeMultiplier=BridgeParticleMath::SizeMultiplier(SizeMultiplier);
    ParticleDensityMultiplier=BridgeParticleMath::DensityMultiplier(DensityMultiplier);
    ParticleLifetimeMultiplier=BridgeParticleMath::LifetimeMultiplier(LifetimeMultiplier);
}
void ABridgeVanillaEffects::ClearParticles() {
    Particles.Empty(); PhysicsClock=0;
    for(auto& Entry:Groups) if(Entry.Value) Entry.Value->DestroyComponent();
    Groups.Empty();
}
void ABridgeVanillaEffects::EndPlay(const EEndPlayReason::Type Reason) { ClearParticles(); Super::EndPlay(Reason); }

bool ABridgeVanillaEffects::ResolveTexture(const FString& BlockId,FColor& Tint,UTexture*& DustTexture) const {
    if(!IsValid(Palette)) return false;
    DustTexture=Palette->FindParticleTexture(BlockId);
    if(IsValid(DustTexture)) Tint=Palette->ParticleTint(BlockId);
    else {
        UMaterialInterface* BlockMaterial=Palette->Find(BlockId); if(!IsValid(BlockMaterial)) return false;
        float TintEnabled=0;
        const bool Grass=BlockId==TEXT("minecraft:grass_block");
        const FName TextureParameter(Grass ? TEXT("BottomTexture") : TEXT("SideTexture"));
        if(!BlockMaterial->GetTextureParameterValue(FMaterialParameterInfo(TextureParameter),DustTexture) || !IsValid(DustTexture)) return false;
        if(!Grass) BlockMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SideTint")),TintEnabled);
        if(TintEnabled<.5f || Grass) Tint=FColor::White;
    }
    if(BlockId==TEXT("minecraft:grass_block")) Tint=FColor::White;
    return true;
}
void ABridgeVanillaEffects::BeginRequest(const FString& Type,const FString& BlockId,int32 Count) {
    CountUp(Diagnostics.Requested,Count); Diagnostics.LastType=Type; Diagnostics.LastBlock=BlockId.Left(128);
    Diagnostics.LastRequested=Count; Diagnostics.LastSpawned=0; Diagnostics.LastReason=TEXT("ready");
}
void ABridgeVanillaEffects::Reject(int32 Count,const FString& Reason) {
    CountUp(Diagnostics.Rejected,FMath::Max(0,Count)); Diagnostics.LastReason=Reason;
    const double Now=FPlatformTime::Seconds();
    if(LastFailureLog<0 || Now-LastFailureLog>=3) {
        UE_LOG(LogTemp,Warning,TEXT("Bridge dust: %s, %s, requested=%d spawned=%d (check /uebridge status; rerun setup_minecraft_visuals for missing material/textures)"),
            *Reason,*Diagnostics.LastBlock,Diagnostics.LastRequested,Diagnostics.LastSpawned);
        LastFailureLog=Now;
    }
}
FString ABridgeVanillaEffects::FindGroup(const FString& BlockId,FColor Tint) {
    const auto State=GetDiagnostics();
    if(State.Reason!=TEXT("ready")) { Reject(Diagnostics.LastRequested,State.Reason); return FString(); }
    UTexture* DustTexture=nullptr;
    if(!ResolveTexture(BlockId,Tint,DustTexture)) { Reject(Diagnostics.LastRequested,TEXT("missing_texture")); return FString(); }
    const FString Key=FString::Printf(TEXT("%s#%u"),*DustTexture->GetPathName(),Tint.ToPackedARGB());
    if(Groups.Contains(Key)) return Key;
    if(Groups.Num()>=64) { Reject(Diagnostics.LastRequested,TEXT("group_limit")); return FString(); }
    auto* Group=NewObject<UInstancedStaticMeshComponent>(this);
    Group->SetupAttachment(RootComponent); Group->SetMobility(EComponentMobility::Movable);
    Group->SetStaticMesh(ParticlePlane); Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Group->SetCanEverAffectNavigation(false); Group->SetGenerateOverlapEvents(false); Group->SetCastShadow(false);
    Group->NumCustomDataFloats=2;
    auto* Dynamic=UMaterialInstanceDynamic::Create(DustMaterial,this);
    if(!IsValid(Dynamic)) { Group->DestroyComponent(); Reject(Diagnostics.LastRequested,TEXT("material_instance")); return FString(); }
    Dynamic->SetTextureParameterValue(TEXT("ParticleTexture"),DustTexture);
    Dynamic->SetVectorParameterValue(TEXT("ParticleColor"),FLinearColor::FromSRGBColor(Tint)*.6f);
    Group->SetMaterial(0,Dynamic); Group->RegisterComponent();
    if(!Group->IsRegistered()) { Group->DestroyComponent(); Reject(Diagnostics.LastRequested,TEXT("component_registration")); return FString(); }
    Groups.Add(Key,Group);
    return Key;
}
bool ABridgeVanillaEffects::AddParticle(const FVector& Position,const FVector& Velocity,const FString& Group) {
    if(Group.IsEmpty()) return false; // FindGroup already accounts for the rejected request.
    if(Particles.Num()>=384) { Reject(1,TEXT("particle_limit")); return false; }
    FDustParticle Particle;
    Particle.Group=Group; Particle.Position=Position; Particle.Previous=Position;
    // Minecraft Particle adds uniformly random velocity, normalizes it, then adds upward motion.
    FVector RandomDirection=Velocity+FVector(FMath::FRandRange(-.4f,.4f),FMath::FRandRange(-.4f,.4f),FMath::FRandRange(-.4f,.4f));
    Particle.Velocity=RandomDirection.GetSafeNormal()*(FMath::FRand()+FMath::FRand()+1)*120+FVector(0,0,200);
    Particle.Size=BridgeParticleMath::QuadSizeCm(FMath::FRand(),ParticleSizeMultiplier);
    Particle.Lifetime=BridgeParticleMath::LifetimeTicks(FMath::FRand(),ParticleLifetimeMultiplier);
    Particle.TextureOffset=FVector2D(FMath::FRand()*.75f,FMath::FRand()*.75f);
    Particles.Add(MoveTemp(Particle));
    CountUp(Diagnostics.Spawned,1); ++Diagnostics.LastSpawned;
    return true;
}
void ABridgeVanillaEffects::SpawnBreak(const FVector& Center,const FString& BlockId,FColor Tint,const TArray<FBox>& MinecraftShapeBoxes) {
    TArray<FBox> Boxes=MinecraftShapeBoxes;
    if(Boxes.IsEmpty()) Boxes.Add(FBox(FVector::ZeroVector,FVector::OneVector));
    if(Boxes.Num()>64) Boxes.SetNum(64);
    Boxes.RemoveAll([](const FBox& Box){return !Box.IsValid || Box.Min.ContainsNaN() || Box.Max.ContainsNaN() || Box.GetSize().GetMin()<=0;});
    int32 NativeCount=0;
    for(const auto& Box:Boxes) {
        const FVector Extent=Box.GetSize();NativeCount+=BridgeParticleMath::BoxCount(Extent.X,Extent.Y,Extent.Z);
    }
    const int32 Selected=BridgeParticleMath::SelectedCount(NativeCount,ParticleDensityMultiplier);
    BeginRequest(TEXT("break"),BlockId,Selected);if(Selected==0) return;
    const FString Group=FindGroup(BlockId,Tint); if(Group.IsEmpty()) return;
    int32 CellIndex=0;
    for(const auto& Box:Boxes) {
        const FVector Extent=Box.GetSize().ComponentMin(FVector::OneVector);
        const int32 NX=BridgeParticleMath::Subdivisions(Extent.X),NY=BridgeParticleMath::Subdivisions(Extent.Y),NZ=BridgeParticleMath::Subdivisions(Extent.Z);
        for(int32 X=0;X<NX;++X) for(int32 Y=0;Y<NY;++Y) for(int32 Z=0;Z<NZ;++Z,++CellIndex) {
            if(!BridgeParticleMath::SelectCell(CellIndex,NativeCount,Selected)) continue;
            const FVector Fraction((X+.5)/NX,(Y+.5)/NY,(Z+.5)/NZ);
            const FVector Local=Box.Min+Fraction*Extent;
            const FVector Offset=BridgeProtocol::ToDirection(Local-FVector(.5))*100;
            const FVector Velocity=BridgeProtocol::ToDirection(Fraction-FVector(.5));
            AddParticle(Center+Offset,Velocity,Group);
        }
    }
}
void ABridgeVanillaEffects::SpawnSprint(const FVector& Feet,const FVector& Velocity,const FString& BlockId,FColor Tint) {
    SprintDensityAccumulator+=BridgeParticleMath::DensityMultiplier(ParticleDensityMultiplier);
    if(SprintDensityAccumulator<1) {BeginRequest(TEXT("sprint"),BlockId,0);return;}
    SprintDensityAccumulator-=1;
    BeginRequest(TEXT("sprint"),BlockId,1);
    const FString Group=FindGroup(BlockId,Tint); if(Group.IsEmpty()) return;
    // Entity's -4*motion / 1.5 upward inputs still pass through Particle's random normalization.
    const FVector SeedVelocity(-4*Velocity.X/2000,-4*Velocity.Y/2000,1.5f);
    AddParticle(Feet+FVector(FMath::FRandRange(-30.f,30.f),FMath::FRandRange(-30.f,30.f),10),SeedVelocity,Group);
}
void ABridgeVanillaEffects::SampleCharacter(ABridgeCharacter* Character,float DeltaSeconds,TArray<FBridgeVanillaEvent>& OutEvents) {
    if(!IsValid(Character) || !Character->UEAuthority || !Terrain || !Terrain->IsSealed()) { ResetMovement(); return; }
    const auto* Movement=Character->GetCharacterMovement(); const auto* Capsule=Character->GetCapsuleComponent();
    if(!Movement || !Capsule) { ResetMovement(); return; }
    const FVector Feet=Character->GetMinecraftFeetPosition();
    const bool Grounded=Movement->IsMovingOnGround();
    if(!HaveMovementSample || SampledCharacter.Get()!=Character || FVector::DistSquared(Feet,PreviousFeet)>40000) {
        SampledCharacter=Character; HaveMovementSample=true; PreviousFeet=Feet; WasGrounded=Grounded;
        FallPeak=Feet.Z; WalkDistance=0; SprintClock=0; return;
    }
    FBridgeVanillaEvent Surface;
    const bool HasSurface=Grounded && Terrain->GetSupportingBlock(Feet,Surface.SourceVoxel,Surface.BlockId,Surface.Tint,Surface.Position,Character);
    if(!Grounded) FallPeak=FMath::Max(FallPeak,static_cast<float>(Feet.Z));
    const bool Landed=Grounded && !WasGrounded && HasSurface;
    if(Landed) {
        const float Distance=FMath::Max(0.f,(FallPeak-static_cast<float>(Feet.Z))*.01f);
        if(Distance>=.25f) { auto Event=Surface; Event.Type=TEXT("land"); Event.FallDistance=Distance; OutEvents.Add(MoveTemp(Event)); }
        WalkDistance=0;
    }
    if(Grounded) FallPeak=Feet.Z;
    const FVector Velocity=Character->GetVelocity();
    const float HorizontalDistance=FVector::Dist2D(Feet,PreviousFeet);
    if(HasSurface && !Character->bIsCrouched && HorizontalDistance>.01f && Velocity.SizeSquared2D()>25) {
        if(!Landed) WalkDistance+=HorizontalDistance;
        // Entity.distanceTraveled adds 0.6*distance and advances one integer for each step.
        if(!Landed && WalkDistance>=100/.6f) { WalkDistance=FMath::Fmod(WalkDistance,100/.6f); auto Event=Surface; Event.Type=TEXT("step"); OutEvents.Add(MoveTemp(Event)); }
        if(Character->IsAuthoritySprinting()) {
            SprintClock+=FMath::Clamp(DeltaSeconds,0.f,.1f);
            while(SprintClock>=.05f) { SprintClock-=.05f; SpawnSprint(Surface.Position,Velocity,Surface.BlockId,Surface.Tint); }
        } else SprintClock=0;
    } else if(Grounded && !HasSurface && Character->IsAuthoritySprinting() && HorizontalDistance>.01f && Velocity.SizeSquared2D()>25) {
        SprintClock+=FMath::Clamp(DeltaSeconds,0.f,.1f);
        if(SprintClock>=.05f) {
            SprintClock=FMath::Fmod(SprintClock,.05f); BeginRequest(TEXT("sprint"),FString(),1); Reject(1,TEXT("missing_surface"));
        }
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
    FQuat Facing=FQuat::Identity;
    if(ViewCamera.IsValid()) {
        const FQuat CameraRotation=ViewCamera->GetComponentQuat();
        Facing=FRotationMatrix::MakeFromXY(CameraRotation.RotateVector(FVector(0,1,0)),CameraRotation.RotateVector(FVector(0,0,1))).ToQuat();
    }
    const float Interpolation=PhysicsClock/.05f;
    TMap<FString,int32> InstanceCounts;
    for(const auto& Particle:Particles) {
        auto* Found=Groups.Find(Particle.Group);
        if(!Found || !IsValid(*Found)) { Reject(0,TEXT("render_group_missing")); continue; }
        auto* Group=Found->Get();
        const FVector RenderPosition=FMath::Lerp(Particle.Previous,Particle.Position,Interpolation);
        const FTransform Transform(Facing,RenderPosition,FVector(Particle.Size*.01f));
        const int32 Instance=InstanceCounts.FindOrAdd(Particle.Group)++;
        // Keep instances alive across frames. Repeated ClearInstances/AddInstance cycles
        // discard render buffers/bounds just before scene capture and obscure diagnostics.
        bool Submitted=Instance<Group->GetInstanceCount()
            ? Group->UpdateInstanceTransform(Instance,Transform,true,false,true)
            : Group->AddInstance(Transform,true)==Instance;
        Submitted=Group->SetCustomDataValue(Instance,0,Particle.TextureOffset.X,false) && Submitted;
        Submitted=Group->SetCustomDataValue(Instance,1,Particle.TextureOffset.Y,false) && Submitted;
        if(!Submitted) Reject(0,TEXT("render_submission"));
    }
    for(auto& Entry:Groups) {
        const auto* Count=InstanceCounts.Find(Entry.Key);
        if(!Count || !IsValid(Entry.Value)) continue;
        // Remove from the tail so particle-to-instance indices stay stable this frame.
        while(Entry.Value->GetInstanceCount()>*Count) {
            if(!Entry.Value->RemoveInstance(Entry.Value->GetInstanceCount()-1)) { Reject(0,TEXT("render_submission")); break; }
        }
        Entry.Value->UpdateBounds(); Entry.Value->MarkRenderStateDirty();
    }
    Diagnostics.PeakInstances=FMath::Max(Diagnostics.PeakInstances,GetDiagnostics().Instances);
    for(auto It=Groups.CreateIterator();It;++It) if(!InstanceCounts.Contains(It.Key())) {
        if(It.Value()) It.Value()->DestroyComponent(); It.RemoveCurrent();
    }
}
