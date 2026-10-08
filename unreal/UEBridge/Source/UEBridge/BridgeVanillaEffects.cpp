#include "BridgeVanillaEffects.h"
#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "BridgeNativeUiPalette.h"
#include "BridgeCharacter.h"
#include "BridgeParticleMath.h"
#include "BridgeProtocol.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

namespace {
// IEEE-754 JSON numbers are exact only through 2^53-1. Saturate long-lived counters.
constexpr uint64 MaxDiagnosticCount=9007199254740991ull;
void CountUp(uint64& Counter,uint64 Amount) { Counter=FMath::Min(MaxDiagnosticCount,Counter+Amount); }
FLinearColor SafeLight(const FLinearColor& Value) {
    auto Channel=[](float V,float Fallback){return FMath::IsFinite(V) ? FMath::Clamp(V,0.f,1.f) : Fallback;};
    return FLinearColor(Channel(Value.R,1),Channel(Value.G,0),Channel(Value.B,1),1);
}
double Gaussian() {
    // Independent standard-normal draws, as LivingEntity.random.nextGaussian.
    const double U=FMath::Max(double(FMath::FRand()),1.e-12),V=double(FMath::FRand());
    return FMath::Sqrt(-2*FMath::Loge(U))*FMath::Cos(2*PI*V);
}
FVector PoofVector(const BridgeDeathPoofMath::Vector& Value) {return FVector(Value.x,Value.y,Value.z);}
}

ABridgeVanillaEffects::ABridgeVanillaEffects() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(PlaneAsset.Succeeded()) ParticlePlane=PlaneAsset.Object;
}
void ABridgeVanillaEffects::ConfigurePoof(UBridgeNativeUiPalette* Resources,UMaterialInterface* Material) {
    if(PoofResources==Resources && PoofMaterial==Material && PoofSetupReason==TEXT("ready")) return;
    ClearPoofParticles();PoofResources=Resources;PoofMaterial=Material;
    UTexture* DefaultTexture=nullptr;
    if(!IsValid(PoofMaterial)) PoofSetupReason=TEXT("missing_material");
    else if(!PoofMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("PoofTexture")),DefaultTexture) || !IsValid(DefaultTexture)) PoofSetupReason=TEXT("material_parameters");
    else if(!PoofMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes)) PoofSetupReason=TEXT("material_instancing");
    else if(!IsValid(PoofResources) || PoofResources->DeathPoofFrames.IsEmpty()) PoofSetupReason=TEXT("missing_active_pack_frames");
    else if(PoofResources->DeathPoofFrames.Num()>256 || PoofResources->DeathPoofFrames.ContainsByPredicate([](const TObjectPtr<UTexture2D>& Texture){return !IsValid(Texture.Get());})) PoofSetupReason=TEXT("invalid_active_pack_frames");
    else if(!IsValid(ParticlePlane)) PoofSetupReason=TEXT("missing_plane");
    else PoofSetupReason=TEXT("ready");
}
FString ABridgeVanillaEffects::DeathPoofReason() const {
    return PoofSetupReason==TEXT("ready") && !ViewCamera.IsValid() ? TEXT("missing_camera") : PoofSetupReason;
}
int32 ABridgeVanillaEffects::SpawnDeathPoof(const FVector& FeetPosition,float WidthCm,float HeightCm) {
    const FString Reason=DeathPoofReason();
    if(Reason!=TEXT("ready") || FeetPosition.ContainsNaN() || !FMath::IsFinite(WidthCm) || !FMath::IsFinite(HeightCm) || WidthCm<=0 || HeightCm<=0) {
        UE_LOG(LogTemp,Warning,TEXT("Bridge death poof: %s; re-export active Minecraft resources and re-import the native package for missing POOF frames/material"),*Reason);
        return 0;
    }
    if(PoofParticles.Num()+BridgeDeathPoofMath::Count>4096) {
        UE_LOG(LogTemp,Warning,TEXT("Bridge death poof: particle_limit (4096)"));return 0;
    }
    if(Particles.IsEmpty() && PoofParticles.IsEmpty()) PhysicsClock=0;
    for(int32 Index=0;Index<BridgeDeathPoofMath::Count;++Index) {
        FPoofParticle Particle;
        const double GX=Gaussian(),GY=Gaussian(),GZ=Gaussian();
        Particle.State.position={FeetPosition.X+BridgeDeathPoofMath::OffsetCm(WidthCm,FMath::FRand(),GX),
            FeetPosition.Y+BridgeDeathPoofMath::OffsetCm(WidthCm,FMath::FRand(),GY),
            FeetPosition.Z+BridgeDeathPoofMath::BodyYcm(HeightCm,FMath::FRand(),GZ)};
        Particle.State.previous=Particle.State.position;
        Particle.State.velocity={BridgeDeathPoofMath::VelocityCmPerTick(GX,FMath::FRand()),
            BridgeDeathPoofMath::VelocityCmPerTick(GY,FMath::FRand()),BridgeDeathPoofMath::VelocityCmPerTick(GZ,FMath::FRand())};
        Particle.Gray=BridgeDeathPoofMath::Gray(FMath::FRand());
        Particle.Size=BridgeDeathPoofMath::QuadSizeCm(FMath::FRand(),FMath::FRand());
        Particle.State.lifetime=BridgeDeathPoofMath::LifetimeTicks(FMath::FRand());
        if(SampleLight) Particle.Light=SafeLight(SampleLight(PoofVector(Particle.State.position)));
        Particle.Light.B=1; // Opaque particles have lightmap lighting, no face diffuse shade.
        PoofParticles.Add(MoveTemp(Particle));
    }
    return BridgeDeathPoofMath::Count;
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
    ClearPoofParticles();
}
void ABridgeVanillaEffects::ClearPoofParticles() {
    PoofParticles.Empty();
    for(auto& Entry:PoofGroups) if(IsValid(Entry.Value)) Entry.Value->DestroyComponent();
    PoofGroups.Empty();
}

UInstancedStaticMeshComponent* ABridgeVanillaEffects::FindPoofGroup(int32 Frame) {
    if(!IsValid(PoofMaterial) || !IsValid(PoofResources) || !PoofResources->DeathPoofFrames.IsValidIndex(Frame)) return nullptr;
    if(auto* Existing=PoofGroups.Find(Frame)) return IsValid(*Existing) ? Existing->Get() : nullptr;
    if(PoofGroups.Num()>=256 || !IsValid(ParticlePlane)) return nullptr;
    UTexture2D* Texture=PoofResources->DeathPoofFrames[Frame].Get();
    if(!IsValid(Texture)) return nullptr;
    auto* Group=NewObject<UInstancedStaticMeshComponent>(this);
    if(!IsValid(Group)) return nullptr;
    Group->SetupAttachment(RootComponent); Group->SetMobility(EComponentMobility::Movable);
    Group->SetStaticMesh(ParticlePlane); Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Group->SetCanEverAffectNavigation(false); Group->SetGenerateOverlapEvents(false); Group->SetCastShadow(false);
    // 0 = grayscale multiplier, 1..3 = captured sky/block/shade light.
    Group->NumCustomDataFloats=4;
    auto* Dynamic=UMaterialInstanceDynamic::Create(PoofMaterial,this);
    if(!IsValid(Dynamic)) {Group->DestroyComponent();return nullptr;}
    Dynamic->SetTextureParameterValue(TEXT("PoofTexture"),Texture);
    Dynamic->SetVectorParameterValue(TEXT("PoofColor"),FLinearColor::White);
    Group->SetMaterial(0,Dynamic); Group->RegisterComponent();
    if(!Group->IsRegistered()) {Group->DestroyComponent();return nullptr;}
    PoofGroups.Add(Frame,Group); return Group;
}

BridgeDeathPoofMath::Vector ABridgeVanillaEffects::ResolvePoofCollision(const BridgeDeathPoofMath::Vector& Position,const BridgeDeathPoofMath::Vector& Movement) const {
    BridgeDeathPoofMath::Vector Result=Movement;
    if(!GetWorld() || (std::abs(Movement.x)<.0001 && std::abs(Movement.y)<.0001 && std::abs(Movement.z)<.0001)) return Result;
    const FVector Start=PoofVector(Position), Delta=PoofVector(Movement);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BridgeDeathPoof),false,const_cast<ABridgeVanillaEffects*>(this));
    FHitResult Hit;
    // ExplosionSmokeParticle uses a 0.2 block sprite quad.  Keep the collision
    // box at that size so poof sprites stop on the same terrain surface rather
    // than sinking into the imported voxel collision.
    if(GetWorld()->SweepSingleByChannel(Hit,Start,Start+Delta,FQuat::Identity,ECC_Visibility,
        FCollisionShape::MakeBox(FVector(10,10,10)),Params)) {
        const FVector Actual=Hit.Location-Start;
        Result={Actual.X,Actual.Y,Actual.Z};
    }
    return Result;
}

void ABridgeVanillaEffects::RenderPoof(const FQuat& Facing,float Interpolation) {
    TMap<int32,int32> InstanceCounts;
    for(const auto& Particle:PoofParticles) {
        const int32 Frame=BridgeDeathPoofMath::Frame(Particle.State.age,Particle.State.lifetime,PoofResources ? PoofResources->DeathPoofFrames.Num() : 0);
        auto* Group=FindPoofGroup(Frame);
        if(!IsValid(Group)) continue;
        const FVector Previous=PoofVector(Particle.State.previous),Current=PoofVector(Particle.State.position);
        const FVector Position=FMath::Lerp(Previous,Current,Interpolation);
        const int32 Instance=InstanceCounts.FindOrAdd(Frame)++;
        const FTransform Transform(Facing,Position,FVector(Particle.Size*.01f));
        bool Submitted=Instance<Group->GetInstanceCount()
            ? Group->UpdateInstanceTransform(Instance,Transform,true,false,true)
            : Group->AddInstance(Transform,true)==Instance;
        Submitted=Group->SetCustomDataValue(Instance,0,Particle.Gray,false) && Submitted;
        Submitted=Group->SetCustomDataValue(Instance,1,Particle.Light.R,false) && Submitted;
        Submitted=Group->SetCustomDataValue(Instance,2,Particle.Light.G,false) && Submitted;
        Submitted=Group->SetCustomDataValue(Instance,3,Particle.Light.B,false) && Submitted;
        if(!Submitted) UE_LOG(LogTemp,Warning,TEXT("Bridge death poof: render_submission"));
    }
    for(auto& Entry:PoofGroups) {
        const int32* Count=InstanceCounts.Find(Entry.Key);
        if(!Count || !IsValid(Entry.Value)) continue;
        while(Entry.Value->GetInstanceCount()>*Count) Entry.Value->RemoveInstance(Entry.Value->GetInstanceCount()-1);
        Entry.Value->UpdateBounds(); Entry.Value->MarkRenderStateDirty();
    }
    for(auto It=PoofGroups.CreateIterator();It;++It) if(!InstanceCounts.Contains(It.Key())) {
        if(IsValid(It.Value())) It.Value()->DestroyComponent(); It.RemoveCurrent();
    }
}

void ABridgeVanillaEffects::EndPlay(const EEndPlayReason::Type Reason) {
    for(auto& Entry:NativeGroups) if(Entry.Value) Entry.Value->DestroyComponent();NativeGroups.Empty();NativeParticles.Empty(); ClearParticles(); Super::EndPlay(Reason); }

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
    // UV offsets 0/1 retain the proven material contract. Native lighting is
    // instance-local, so one texture group can span sunlight and a dark room.
    Group->NumCustomDataFloats=5;
    auto* Dynamic=UMaterialInstanceDynamic::Create(DustMaterial,this);
    if(!IsValid(Dynamic)) { Group->DestroyComponent(); Reject(Diagnostics.LastRequested,TEXT("material_instance")); return FString(); }
    Dynamic->SetTextureParameterValue(TEXT("ParticleTexture"),DustTexture);
    Dynamic->SetVectorParameterValue(TEXT("ParticleColor"),FLinearColor(Tint.R/255.f*.6f,Tint.G/255.f*.6f,Tint.B/255.f*.6f,1));
    Dynamic->SetScalarParameterValue(TEXT("BridgeUseVertexLight"),1.f);
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
    if(SampleLight) Particle.Light=SafeLight(SampleLight(Position));
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
            SprintClock=FMath::Fmod(SprintClock,.05f); BeginRequest(TEXT("sprint"),FString(),1); Reject(1,TEXT("missing_surface: ")+Terrain->GetSurfaceReason());
        }
    } else SprintClock=0;
    PreviousFeet=Feet; WasGrounded=Grounded;
}
void ABridgeVanillaEffects::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    TickNativeParticles(DeltaSeconds);
    if(Particles.IsEmpty() && PoofParticles.IsEmpty()) return;
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
            if(SampleLight) Particle.Light=SafeLight(SampleLight(Particle.Position));
        }
        Particles.RemoveAll([](const FDustParticle& Particle){return Particle.Age>Particle.Lifetime;});
        for(auto& Particle:PoofParticles) {
            Particle.State.Tick([this](const BridgeDeathPoofMath::Vector& Position,const BridgeDeathPoofMath::Vector& Motion){return ResolvePoofCollision(Position,Motion);});
            if(SampleLight) Particle.Light=SafeLight(SampleLight(PoofVector(Particle.State.position)));
            Particle.Light.B=1;
        }
        PoofParticles.RemoveAll([](const FPoofParticle& Particle){return Particle.State.age>Particle.State.lifetime;});
    }
    FQuat Facing=FQuat::Identity;
    if(ViewCamera.IsValid()) {
        const FQuat CameraRotation=ViewCamera->GetComponentQuat();
        Facing=FRotationMatrix::MakeFromXY(CameraRotation.RotateVector(FVector(0,1,0)),CameraRotation.RotateVector(FVector(0,0,1))).ToQuat();
    }
    const float Interpolation=PhysicsClock/.05f;
    RenderPoof(Facing,Interpolation);
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
        Submitted=Group->SetCustomDataValue(Instance,2,Particle.Light.R,false) && Submitted;
        Submitted=Group->SetCustomDataValue(Instance,3,Particle.Light.G,false) && Submitted;
        Submitted=Group->SetCustomDataValue(Instance,4,Particle.Light.B,false) && Submitted;
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

void ABridgeVanillaEffects::SpawnCombat(const FVector& Position,const FVector& Direction,bool Critical,bool Sweep) {
    if(!PoofResources || !PoofMaterial || NativeParticles.Num()>=2048) return;
    if(Critical) for(int32 I=0;I<32 && NativeParticles.Num()<2048;++I) {
        auto& Particle=NativeParticles.AddDefaulted_GetRef();Particle.Sprite=TEXT("particle/critical_hit");
        const FVector Random=FMath::VRand();Particle.Position=Position+Random*40;Particle.Velocity=Random*180+FVector(0,0,80);
        Particle.Lifetime=FMath::FRandRange(.2f,.4f);Particle.Size=FMath::FRandRange(8.f,16.f);Particle.Gravity=320;
    }
    if(Sweep && NativeParticles.Num()<2048) {auto& Particle=NativeParticles.AddDefaulted_GetRef();Particle.Sprite=TEXT("particle/sweep_0");
        Particle.Position=Position+Direction.GetSafeNormal2D()*40;Particle.Sweep=true;Particle.Size=150;Particle.Lifetime=.4f;}
}
void ABridgeVanillaEffects::TickNativeParticles(float DeltaSeconds) {
    if(!PoofResources || !PoofMaterial || !ParticlePlane || !GetWorld()) return;
    const float Dt=FMath::Clamp(DeltaSeconds,0.f,.1f);LeafClock+=Dt;
    if(LeafClock>=.05f && SampledCharacter.IsValid() && Terrain && NativeParticles.Num()<1024) {
        LeafClock=FMath::Fmod(LeafClock,.05f);
        for(int32 I=0;I<24;++I) {
            const FVector Sample=SampledCharacter->GetMinecraftFeetPosition()+FVector(FMath::FRandRange(-1200.f,1200.f),FMath::FRandRange(-1200.f,1200.f),FMath::FRandRange(100.f,1200.f));
            const FIntVector Voxel=Terrain->SourceVoxelAt(Sample);FString Id,State,Below,BelowState;
            if(!Terrain->GetBlockState(Voxel,Id,State) || !Id.EndsWith(TEXT("_leaves")) || Terrain->GetBlockState(Voxel-FIntVector(0,1,0),Below,BelowState)) continue;
            FString Kind=Id==TEXT("minecraft:cherry_leaves") ? TEXT("cherry_") : Id==TEXT("minecraft:pale_oak_leaves") ? TEXT("pale_oak_") : TEXT("leaf_");
            const FString Sprite=TEXT("particle/")+Kind+FString::FromInt(FMath::RandRange(0,11));if(!PoofResources->FindSprite(Sprite)) continue;
            auto& Particle=NativeParticles.AddDefaulted_GetRef();Particle.Sprite=Sprite;Particle.Leaf=true;
            Particle.Position=Terrain->BlockCenter(Voxel)+FVector(FMath::FRandRange(-45.f,45.f),FMath::FRandRange(-45.f,45.f),-50);
            Particle.Velocity=FVector(FMath::FRandRange(-12.f,12.f),FMath::FRandRange(-12.f,12.f),-8);
            Particle.Size=FMath::FRandRange(8.f,16.f);Particle.Gravity=12;Particle.Lifetime=FMath::FRandRange(8.f,15.f);Particle.Spin=FMath::FRandRange(0.f,6.28f);
            if(Kind==TEXT("leaf_")) Particle.Tint=Terrain->RenderTintAt(Voxel,Id);
        }
    }
    for(auto& Particle:NativeParticles) {
        Particle.Age+=Dt;if(Particle.Sweep) {Particle.Sprite=TEXT("particle/sweep_")+FString::FromInt(FMath::Clamp(int32(Particle.Age*20),0,7));continue;}
        Particle.Velocity.Z-=Particle.Gravity*Dt;
        FVector Delta=Particle.Velocity*Dt;
        if(Particle.Leaf) {Delta.X+=FMath::Sin(Particle.Age*2+Particle.Spin)*20*Dt;Delta.Y+=FMath::Cos(Particle.Age*1.7f+Particle.Spin)*20*Dt;}
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(NativeParticle),false,this);if(SampledCharacter.IsValid()) Query.AddIgnoredActor(SampledCharacter.Get());
        if(GetWorld()->LineTraceSingleByChannel(Hit,Particle.Position,Particle.Position+Delta,ECC_WorldStatic,Query)) {Particle.Position=Hit.ImpactPoint+Hit.Normal;Particle.Velocity=FVector::ZeroVector;if(Particle.Leaf) Particle.Lifetime=FMath::Min(Particle.Lifetime,Particle.Age+.2f);}
        else Particle.Position+=Delta;
        if(!Particle.Leaf) Particle.Velocity*=FMath::Pow(.7f,Dt*20);
    }
    NativeParticles.RemoveAll([](const FNativeParticle& P){return P.Age>=P.Lifetime;});
    FQuat Facing=FQuat::Identity;if(ViewCamera.IsValid()) {const FQuat Camera=ViewCamera->GetComponentQuat();Facing=FRotationMatrix::MakeFromXY(Camera.RotateVector(FVector(0,1,0)),Camera.RotateVector(FVector(0,0,1))).ToQuat();}
    TMap<FString,int32> Counts;
    for(const auto& Particle:NativeParticles) {
        UTexture2D* Texture=PoofResources->FindSprite(Particle.Sprite);if(!Texture) continue;
        const FString Key=Particle.Sprite+FString::Printf(TEXT("#%u"),Particle.Tint.ToPackedARGB());if(!NativeGroups.Contains(Key) && NativeGroups.Num()>=128) continue;auto& Group=NativeGroups.FindOrAdd(Key);
        if(!Group && NativeGroups.Num()<=128) {
            Group=NewObject<UInstancedStaticMeshComponent>(this);Group->SetupAttachment(RootComponent);Group->SetMobility(EComponentMobility::Movable);Group->SetStaticMesh(ParticlePlane);
            Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);Group->SetCastShadow(false);Group->SetCanEverAffectNavigation(false);Group->NumCustomDataFloats=4;
            auto* Material=UMaterialInstanceDynamic::Create(PoofMaterial,this);Material->SetTextureParameterValue(TEXT("PoofTexture"),Texture);
            Material->SetVectorParameterValue(TEXT("PoofColor"),FLinearColor(Particle.Tint.R/255.f,Particle.Tint.G/255.f,Particle.Tint.B/255.f,1));Group->SetMaterial(0,Material);Group->RegisterComponent();
        }
        if(!Group) continue;const int32 Index=Counts.FindOrAdd(Key)++;
        const FQuat Rotation=Particle.Leaf ? Facing*FQuat(FVector::UpVector,Particle.Spin+Particle.Age) : Facing;
        const FTransform Transform(Rotation,Particle.Position,FVector(Particle.Size*.01f));
        if(Index<Group->GetInstanceCount()) Group->UpdateInstanceTransform(Index,Transform,true,false,true);else Group->AddInstance(Transform,true);
        FLinearColor Light=SampleLight ? SafeLight(SampleLight(Particle.Position)) : FLinearColor(1,0,1,1);
        Group->SetCustomDataValue(Index,0,1,false);Group->SetCustomDataValue(Index,1,Light.R,false);Group->SetCustomDataValue(Index,2,Light.G,false);Group->SetCustomDataValue(Index,3,Light.B,false);
    }
    for(auto It=NativeGroups.CreateIterator();It;++It) {
        auto* Group=It.Value().Get();const int32 Count=Counts.FindRef(It.Key());
        if(!Group || !Count) {if(Group) Group->DestroyComponent();It.RemoveCurrent();continue;}
        while(Group->GetInstanceCount()>Count) Group->RemoveInstance(Group->GetInstanceCount()-1);Group->MarkRenderStateDirty();
    }
}
