#include "BridgeMobWorld.h"
#include "BridgeMobCharacter.h"
#include "BridgeProtocol.h"
#include "BridgeMobSpawnMath.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

ABridgeMobWorld::ABridgeMobWorld() { PrimaryActorTick.bCanEverTick=false; }

bool ABridgeMobWorld::Import(const FBridgeMobSnapshot& Snapshot,const FVector& Anchor) {
    if(SeenIds.Contains(Snapshot.Id)) {LastReason=TEXT("already_imported");return true;} // Retried receipts must never resurrect killed copies.
    if(!GetWorld()) return Reject(TEXT("world_unavailable"),Snapshot.Type,Snapshot.Id);
    if(!Palette) return Reject(TEXT("missing_palette: assign MobPalette to the saved BridgeReceiver"),Snapshot.Type,Snapshot.Id);
    if(Snapshot.Id.IsEmpty()) return Reject(TEXT("invalid_mob_id"),Snapshot.Type,Snapshot.Id);
    if(AliveCount()>=128 || SeenIds.Num()>=4096) return Reject(TEXT("mob_limit: 128 alive / 4096 spawn history"),Snapshot.Type,Snapshot.Id);
    const FBridgeMobAppearance* Appearance=Palette->Find(Snapshot.Appearance);
    if(!Appearance) {MissingAppearance++;return Reject(TEXT("appearance_not_in_assigned_palette"),Snapshot.Type,Snapshot.Id);}
    if(Appearance->Type!=Snapshot.Type) return Reject(TEXT("appearance_species_mismatch"),Snapshot.Type,Snapshot.Id);
    if(!Appearance->Material) return Reject(TEXT("appearance_material_missing"),Snapshot.Type,Snapshot.Id);
    if(Appearance->Parts.IsEmpty()) return Reject(TEXT("appearance_parts_empty"),Snapshot.Type,Snapshot.Id);
    FVector Feet=BridgeProtocol::ToUnreal(Snapshot.Position,Anchor);
    if(SpawnAllowed && !SpawnAllowed(Feet)) return Reject(TEXT("mob_position_outside_loaded_terrain"),Snapshot.Type,Snapshot.Id);
    if(PrepareSpawnCollision) PrepareSpawnCollision(Feet);
    const float Half=FMath::Clamp(Snapshot.Height*50.f,10.f,1000.f),Radius=FMath::Clamp(Snapshot.Width*50.f,5.f,Half);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeMobImport),false,this);if(Player.IsValid()) Query.AddIgnoredActor(Player.Get());
    const FVector CapsuleCenter=Feet+FVector(0,0,Half+2);
    const FVector PlayerDelta=Player.IsValid() ? CapsuleCenter-Player->GetActorLocation() : FVector::ZeroVector;
    const bool PlayerBlocked=Player.IsValid() && BridgeMobSpawnMath::CapsulesOverlap(PlayerDelta.SizeSquared2D(),PlayerDelta.Z,Radius,Half,Player->GetSimpleCollisionRadius(),Player->GetSimpleCollisionHalfHeight());
    if(PlayerBlocked || GetWorld()->OverlapBlockingTestByChannel(CapsuleCenter,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)) {
        FVector SafeFeet;FString Reason;
        if(!FindSpawnFeet(Feet,Radius,Half,SafeFeet,Reason)) return Reject(Reason,Snapshot.Type,Snapshot.Id);
        Feet=SafeFeet;
    } else Feet.Z+=2;
    FVector Position=Feet+FVector(0,0,Snapshot.Height*50.f);
    FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABridgeMobCharacter* Mob=GetWorld()->SpawnActor<ABridgeMobCharacter>(Position,BridgeProtocol::ToRotation(Snapshot.Yaw,0),Params);
    if(!Mob) return Reject(TEXT("actor_spawn_failed"),Snapshot.Type,Snapshot.Id);
    if(!Mob->Initialize(Snapshot,*Appearance,this)) {
        const FString Reason=TEXT("model_initialization_failed: ")+Mob->ModelDiagnostic();Mob->Destroy();return Reject(Reason,Snapshot.Type,Snapshot.Id);
    }
    Mob->SetAuthority(Authority,Player.Get());Mobs.Add(Mob);SeenIds.Add(Snapshot.Id);Imported++;LastReason=TEXT("ready");
    UE_LOG(LogTemp,Display,TEXT("Bridge mob ready: type=%s id=%s key=%s palette=%s parts=%d vertices=%d"),*Snapshot.Type,*Snapshot.Id,*Snapshot.Appearance,*GetNameSafe(Palette),Appearance->Parts.Num(),Mob->ModelVertexCount());
    return true;
}

FString ABridgeMobWorld::SpawnEgg(const FString& Type,const FVector& Feet,const FVector& Anchor,const FString& Id) {
    auto Fail=[&](const FString& Reason){Reject(Reason,Type,Id);return LastReason;};
    if(SeenIds.Contains(Id)) {LastReason=TEXT("mob already spawned: ")+Type;return LastReason;}
    if(!Authority) return Fail(TEXT("mob controller not ready"));
    if(!Palette) return Fail(TEXT("mob palette missing: assign the imported palette to BridgeReceiver"));
    FString Key;const FBridgeMobAppearance* Appearance=ResolveTemplate(Type,Key);
    if(!Appearance) {MissingAppearance++;return Fail(TEXT("mob template missing: ")+Type+TEXT("; /uebridge mobs export then setup_minecraft_mobs"));}
    if(!Appearance->Material || Appearance->Parts.IsEmpty()) return Fail(TEXT("mob template model incomplete: ")+Type);
    if(AliveCount()>=128 || SeenIds.Num()>=4096) return Fail(TEXT("mob limit reached: 128 alive / 4096 history"));
    if(PrepareSpawnCollision) PrepareSpawnCollision(Feet);
    FBridgeMobSnapshot Snapshot;Snapshot.Id=Id;Snapshot.Type=Type;Snapshot.Appearance=Key;
    Snapshot.Width=Appearance->Width;Snapshot.Height=Appearance->Height;Snapshot.MaxHealth=Appearance->MaxHealth;Snapshot.Health=Snapshot.MaxHealth;
    Snapshot.Speed=Appearance->Speed;Snapshot.Damage=Appearance->Damage;Snapshot.Hostile=Appearance->Hostile;Snapshot.Baby=Appearance->Baby;
    const float Half=FMath::Clamp(Snapshot.Height*50.f,10.f,1000.f),Radius=FMath::Clamp(Snapshot.Width*50.f,5.f,Half);
    FVector SafeFeet;FString Reason;
    if(!FindSpawnFeet(Feet,Radius,Half,SafeFeet,Reason)) return Fail(Reason);
    const FVector Relative=(SafeFeet-Anchor)/100;Snapshot.Position=FVector(-Relative.Y,Relative.Z,Relative.X);
    if(!Import(Snapshot,Anchor)) return LastReason;
    LastReason=TEXT("mob spawned: ")+Type;return LastReason;
}

bool ABridgeMobWorld::Reject(const FString& Reason,const FString& Type,const FString& Id) {
    Rejected++;LastReason=Reason;
    UE_LOG(LogTemp,Warning,TEXT("Bridge mob rejected: stage=%s type=%s id=%s palette=%s appearances=%d templates=%d"),*Reason,*Type,*Id,*GetNameSafe(Palette),Palette ? Palette->Appearances.Num() : 0,Palette ? Palette->Templates.Num() : 0);
    return false;
}

const FBridgeMobAppearance* ABridgeMobWorld::ResolveTemplate(const FString& Type,FString& Key) const {
    if(!Palette) return nullptr;
    const FString* Explicit=Palette->Templates.Find(Type);
    const FBridgeMobAppearance* Appearance=Explicit ? Palette->Find(*Explicit) : nullptr;
    if(Appearance && Appearance->Type==Type && !Appearance->Baby) {Key=*Explicit;return Appearance;}
    for(const FBridgeMobAppearance& Candidate:Palette->Appearances) if(Candidate.Type==Type && !Candidate.Baby && Candidate.Material && !Candidate.Parts.IsEmpty()) {
        Key=Candidate.Key;return &Candidate;
    }
    return nullptr;
}

bool ABridgeMobWorld::FindSpawnFeet(const FVector& Requested,float Radius,float HalfHeight,FVector& Feet,FString& Reason) const {
    if(!GetWorld()) {Reason=TEXT("mob world unavailable");return false;}
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeEggSpawn),false,this);if(Player.IsValid()) Query.AddIgnoredActor(Player.Get());
    bool SawFloor=false,SawWorld=false,SawPlayer=false,SawWall=false;
    for(const auto& Offset:BridgeMobSpawnMath::Candidates(Radius)) {
        const FVector Seed=Requested+FVector(Offset[0],Offset[1],Offset[2]);
        FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,Seed+FVector(0,0,50),Seed-FVector(0,0,120),ECC_Visibility,Query) || Floor.ImpactNormal.Z<.6f) continue;
        SawFloor=true;
        const FVector Candidate=FVector(Seed.X,Seed.Y,Floor.ImpactPoint.Z+2),Center=Candidate+FVector(0,0,HalfHeight);
        if(SpawnAllowed && !SpawnAllowed(Candidate)) continue;
        // A nearby search must not teleport a mob through the selected wall or closed door.
        FHitResult Between;
        if(!FVector2D(Offset[0],Offset[1]).IsNearlyZero() && GetWorld()->LineTraceSingleByChannel(Between,Requested+FVector(0,0,HalfHeight),Center,ECC_Visibility,Query)) {SawWall=true;continue;}
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,HalfHeight),Query)) {SawWorld=true;continue;}
        if(Player.IsValid()) {
            const FVector Delta=Center-Player->GetActorLocation();
            if(BridgeMobSpawnMath::CapsulesOverlap(Delta.SizeSquared2D(),Delta.Z,Radius,HalfHeight,Player->GetSimpleCollisionRadius(),Player->GetSimpleCollisionHalfHeight())) {SawPlayer=true;continue;}
        }
        Feet=Candidate;return true;
    }
    Reason=!SawFloor ? TEXT("mob spawn blocked: no supported floor within 1.2 blocks")
        : SawPlayer && !SawWorld && !SawWall ? TEXT("mob spawn blocked: player capsule; move away")
        : SawWall ? TEXT("mob spawn blocked: wall/door between spawn candidates") : TEXT("mob spawn blocked: terrain or another mob");
    return false;
}

void ABridgeMobWorld::SetAuthority(bool Active,ACharacter* NewPlayer) {
    Authority=Active;Player=NewPlayer;
    for(ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob)) {
        const FVector Feet=Mob->GetActorLocation()-FVector(0,0,Mob->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        // A retained far copy must not fall through a streamed-out terrain cell.
        Mob->SetAuthority(Active && (!SpawnAllowed || SpawnAllowed(Feet)),NewPlayer);
    }
}
void ABridgeMobWorld::Clear() {
    for(ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob)) Mob->Destroy();Mobs.Reset();SeenIds.Reset();
    Imported=MissingAppearance=Rejected=0;PlayerHealth=20;LastPlayerDamage=-1;LastReason=TEXT("not_imported");
}
void ABridgeMobWorld::EndPlay(const EEndPlayReason::Type Reason) { Clear();Super::EndPlay(Reason); }

bool ABridgeMobWorld::Attack(const FVector& Eye,const FVector& Direction,float Reach,float Damage) {
    if(!Authority || PlayerHealth<=0 || !GetWorld()) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeMobAttack),false,Player.Get());
    FHitResult Hit;
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Direction.GetSafeNormal()*FMath::Clamp(Reach,0.f,600.f),ECC_Visibility,Query)) return false;
    ABridgeMobCharacter* Mob=Cast<ABridgeMobCharacter>(Hit.GetActor());
    return Mob && Mob->Hit(Damage,Direction);
}
bool ABridgeMobWorld::ReceiveArrow(const FVector& From,const FVector& To,float Damage) {
    if(!Authority || !GetWorld()) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeMobArrow),false,Player.Get());FHitResult Hit;
    if(!GetWorld()->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility,Query)) return false;
    ABridgeMobCharacter* Mob=Cast<ABridgeMobCharacter>(Hit.GetActor());return Mob && Mob->Hit(Damage,To-From);
}
void ABridgeMobWorld::NotifyMobSound(const FString& Type,const FString& Suffix,const FVector& Location) {
    FString Species=Type;Species.RemoveFromStart(TEXT("minecraft:"));
    if(Sound) Sound(FString::Printf(TEXT("minecraft:entity.%s.%s"),*Species,*Suffix),Location);
}
void ABridgeMobWorld::HitPlayer(float Damage,const FVector& Location) {
    if(!Authority || Creative || PlayerHealth<=0 || !GetWorld()) return;
    double Now=GetWorld()->GetTimeSeconds();if(LastPlayerDamage>=0 && Now-LastPlayerDamage<.5) return;
    LastPlayerDamage=Now;PlayerHealth=FMath::Max(0.f,PlayerHealth-Damage);
    if(Sound) Sound(PlayerHealth>0 ? TEXT("minecraft:entity.player.hurt") : TEXT("minecraft:entity.player.death"),Location);
}
int32 ABridgeMobWorld::AliveCount() const { int32 Count=0;for(const ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob) && Mob->Alive()) Count++;return Count; }
TArray<FVector> ABridgeMobWorld::CollisionAnchors() const {
    TArray<FVector> Feet;
    for(const ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob) && Mob->Alive()) Feet.Add(Mob->GetActorLocation()-FVector(0,0,Mob->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    return Feet;
}
void ABridgeMobWorld::RespawnPlayer() { PlayerHealth=20;LastPlayerDamage=GetWorld() ? GetWorld()->GetTimeSeconds()+1.f : -1; }
