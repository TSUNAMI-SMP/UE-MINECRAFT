#include "BridgeMobWorld.h"
#include "BridgeMobCharacter.h"
#include "BridgeProtocol.h"
#include "BridgeMobSpawnMath.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

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
    // Exported feet already define the bounding box. Raising a hanging bat by
    // 2 cm pushes its head into the ceiling and forces a ground-only fallback.
    const FVector CapsuleCenter=Feet+FVector(0,0,Half);
    const FVector PlayerDelta=Player.IsValid() ? CapsuleCenter-Player->GetActorLocation() : FVector::ZeroVector;
    const bool PlayerBlocked=Player.IsValid() && BridgeMobSpawnMath::CapsulesOverlap(PlayerDelta.SizeSquared2D(),PlayerDelta.Z,Radius,Half,Player->GetSimpleCollisionRadius(),Player->GetSimpleCollisionHalfHeight());
    if(PlayerBlocked || GetWorld()->OverlapBlockingTestByChannel(CapsuleCenter,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)) {
        FVector SafeFeet;FString Reason;
        if(Snapshot.Type==TEXT("minecraft:bat")) {
            bool Found=false;
            // Resolve ceiling contact locally in open air, without requiring a
            // floor or moving through it. Keep model/data errors fatal.
            for(float Drop=2;Drop<=50;Drop+=2) {
                const FVector Candidate=Feet-FVector(0,0,Drop),Center=Candidate+FVector(0,0,Half);
                FHitResult Barrier;
                if(GetWorld()->LineTraceSingleByChannel(Barrier,Feet,Candidate,ECC_Visibility,Query)) break;
                if(SpawnAllowed && !SpawnAllowed(Candidate)) continue;
                if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)) continue;
                if(Player.IsValid()) {
                    const FVector Delta=Center-Player->GetActorLocation();
                    if(BridgeMobSpawnMath::CapsulesOverlap(Delta.SizeSquared2D(),Delta.Z,Radius,Half,Player->GetSimpleCollisionRadius(),Player->GetSimpleCollisionHalfHeight())) continue;
                }
                SafeFeet=Candidate;Found=true;break;
            }
            if(!Found) return Reject(TEXT("bat restore blocked: no clear air below ceiling within 0.5 blocks"),Snapshot.Type,Snapshot.Id);
        } else if(!FindSpawnFeet(Feet,Radius,Half,SafeFeet,Reason)) return Reject(Reason,Snapshot.Type,Snapshot.Id);
        Feet=SafeFeet;
    }
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
    if(AliveCount()+PendingRestores.Num()>=128 || SeenIds.Num()>=4096) return Fail(TEXT("mob limit reached: 128 alive or pending / 4096 history"));
    if(PrepareSpawnCollision) PrepareSpawnCollision(Feet);
    FBridgeMobSnapshot Snapshot;Snapshot.Id=Id;Snapshot.Type=Type;Snapshot.Appearance=Key;
    Snapshot.Width=Appearance->Width;Snapshot.Height=Appearance->Height;Snapshot.MaxHealth=Appearance->MaxHealth;Snapshot.Health=Snapshot.MaxHealth;
    Snapshot.KnockbackResistance=Appearance->KnockbackResistance;Snapshot.Speed=Appearance->Speed;Snapshot.Damage=Appearance->Damage;Snapshot.Hostile=Appearance->Hostile;Snapshot.Baby=Appearance->Baby;
    const float Half=FMath::Clamp(Snapshot.Height*50.f,10.f,1000.f),Radius=FMath::Clamp(Snapshot.Width*50.f,5.f,Half);
    FVector SafeFeet;FString Reason;
    if(!FindSpawnFeet(Feet,Radius,Half,SafeFeet,Reason)) return Fail(Reason);
    const FVector Relative=(SafeFeet-Anchor)/100;Snapshot.Position=FVector(-Relative.Y,Relative.Z,Relative.X);
    if(!Import(Snapshot,Anchor)) return LastReason;
    LastReason=TEXT("mob spawned: ")+Type;return LastReason;
}

bool ABridgeMobWorld::Reject(const FString& Reason,const FString& Type,const FString& Id) {
    Rejected++;LastReason=Reason;
    if(!QuietRestoreRetry) UE_LOG(LogTemp,Warning,TEXT("Bridge mob rejected: stage=%s type=%s id=%s palette=%s appearances=%d templates=%d"),*Reason,*Type,*Id,*GetNameSafe(Palette),Palette ? Palette->Appearances.Num() : 0,Palette ? Palette->Templates.Num() : 0);
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
    if(Active && !PendingRestores.IsEmpty() && GetWorld() && GetWorld()->GetTimeSeconds()>=NextRestoreRetry) {
        NextRestoreRetry=GetWorld()->GetTimeSeconds()+5;
        QuietRestoreRetry=true;
        for(int32 I=PendingRestores.Num()-1;I>=0;--I) {
            const auto& Pending=PendingRestores[I];
            if(Import(Pending.Snapshot,Pending.Anchor)) {
                Mobs.Last()->SetNativeViewPitch(Pending.Pitch);
                PendingRestores.RemoveAt(I);
            }
        }
        QuietRestoreRetry=false;
    }
    for(ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob)) {
        const FVector Feet=Mob->GetActorLocation()-FVector(0,0,Mob->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        // A retained far copy must not fall through a streamed-out terrain cell.
        Mob->SetAuthority(Active && (!SpawnAllowed || SpawnAllowed(Feet)),NewPlayer);
    }
}
void ABridgeMobWorld::Clear() {
    for(ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob)) Mob->Destroy();Mobs.Reset();SeenIds.Reset();
    PendingRestores.Reset();NextRestoreRetry=0;QuietRestoreRetry=false;
    Imported=MissingAppearance=Rejected=0;PlayerHealth=20;LastPlayerDamage=-1;LastReason=TEXT("not_imported");
}
void ABridgeMobWorld::EndPlay(const EEndPlayReason::Type Reason) { Clear();Super::EndPlay(Reason); }

bool ABridgeMobWorld::Attack(const FVector& Eye,const FVector& Direction,float Reach,float Damage) {
    if(!Authority || PlayerHealth<=0 || !GetWorld()) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeMobAttack),false,Player.Get());
    FHitResult Hit;
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Direction.GetSafeNormal()*FMath::Clamp(Reach,0.f,600.f),ECC_Visibility,Query)) return false;
    ABridgeMobCharacter* Mob=Cast<ABridgeMobCharacter>(Hit.GetActor());
    if(!Mob) return false;Mob->Hit(Damage,Direction);return true; // Consume aim even during the hurt window; never mine through a mob.
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
    for(const auto& Pending:PendingRestores) Feet.Add(BridgeProtocol::ToUnreal(Pending.Snapshot.Position,Pending.Anchor));
    for(const ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob) && Mob->Alive()) Feet.Add(Mob->GetActorLocation()-FVector(0,0,Mob->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    return Feet;
}
void ABridgeMobWorld::RespawnPlayer() { PlayerHealth=20;LastPlayerDamage=GetWorld() ? GetWorld()->GetTimeSeconds()+1.f : -1; }

TArray<TSharedPtr<FJsonValue>> ABridgeMobWorld::ExportNativeSnapshots(const FVector& Anchor,const FVector& SourceOrigin) const {
    TArray<TSharedPtr<FJsonValue>> Result;
    // Preserve the original canonical coordinates and all captured attributes.
    // A blocked copy is not dead and must never disappear from the next save.
    for(const auto& Pending:PendingRestores) Result.Add(Pending.SavedValue);
    for(const ABridgeMobCharacter* Mob:Mobs) {
        if(!IsValid(Mob) || !Mob->Alive()) continue;
        const auto Snapshot=Mob->NativeSnapshot(Anchor,SourceOrigin);
        auto Json=MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("id"),Snapshot.Id);Json->SetStringField(TEXT("type"),Snapshot.Type);
        Json->SetStringField(TEXT("appearance"),Snapshot.Appearance);
        Json->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(Snapshot.Position.X),MakeShared<FJsonValueNumber>(Snapshot.Position.Y),MakeShared<FJsonValueNumber>(Snapshot.Position.Z)});
        Json->SetNumberField(TEXT("yaw"),Snapshot.Yaw);Json->SetNumberField(TEXT("pitch"),Mob->GetNativeViewPitch());
        Json->SetNumberField(TEXT("width"),Snapshot.Width);Json->SetNumberField(TEXT("height"),Snapshot.Height);
        Json->SetNumberField(TEXT("health"),Snapshot.Health);Json->SetNumberField(TEXT("maxHealth"),Snapshot.MaxHealth);
        Json->SetNumberField(TEXT("knockbackResistance"),Snapshot.KnockbackResistance);Json->SetNumberField(TEXT("speed"),Snapshot.Speed);Json->SetNumberField(TEXT("damage"),Snapshot.Damage);
        Json->SetBoolField(TEXT("hostile"),Snapshot.Hostile);Json->SetBoolField(TEXT("baby"),Snapshot.Baby);
        Result.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Result;
}

bool ABridgeMobWorld::ImportNativeSnapshots(const TArray<TSharedPtr<FJsonValue>>& Snapshots,const FVector& Anchor,const FVector& SourceOrigin) {
    if(Snapshots.Num()>128) return Reject(TEXT("native_mob_save_limit"),FString(),FString());
    if(!Snapshots.IsEmpty() && !Palette) return Reject(TEXT("native_mob_palette_missing"),FString(),FString());
    TArray<FBridgeMobSnapshot> Parsed;TArray<float> Pitch;TSet<FString> UniqueIds;
    const FString EnvelopeId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    for(const auto& Value:Snapshots) {
        if(!Value.IsValid() || Value->Type!=EJson::Object) return Reject(TEXT("native_mob_save_object"),FString(),FString());
        const auto Json=Value->AsObject();FString Id,Type,AppearanceKey;
        if(!Json.IsValid() || !Json->TryGetStringField(TEXT("id"),Id) || !Json->TryGetStringField(TEXT("type"),Type)
            || !Json->TryGetStringField(TEXT("appearance"),AppearanceKey) || UniqueIds.Contains(Id))
            return Reject(TEXT("native_mob_save_identity"),Type,Id);
        const auto* Appearance=Palette->Find(AppearanceKey);
        if(!Appearance || Appearance->Type!=Type) return Reject(TEXT("native_mob_saved_appearance_missing"),Type,Id);
        const TArray<TSharedPtr<FJsonValue>>* Position=nullptr;
        if(!Json->TryGetArrayField(TEXT("position"),Position) || Position->Num()!=3) return Reject(TEXT("native_mob_save_position"),Type,Id);
        auto Packet=MakeShared<FJsonObject>();Packet->Values=Json->Values;
        Packet->SetNumberField(TEXT("v"),1);Packet->SetStringField(TEXT("kind"),TEXT("event"));Packet->SetNumberField(TEXT("seq"),1);
        Packet->SetStringField(TEXT("session"),EnvelopeId);Packet->SetStringField(TEXT("eventId"),EnvelopeId);
        Packet->SetStringField(TEXT("event"),TEXT("mob_spawn"));Packet->SetStringField(TEXT("importId"),EnvelopeId);
        Packet->SetStringField(TEXT("mobId"),Id);Packet->SetStringField(TEXT("mobType"),Type);
        const TCHAR* Axes[]={TEXT("x"),TEXT("y"),TEXT("z")};
        for(int32 I=0;I<3;++I) {
            double Coordinate=0;
            if(!(*Position)[I].IsValid() || !(*Position)[I]->TryGetNumber(Coordinate) || !FMath::IsFinite(Coordinate) || FMath::Abs(Coordinate)>30000000)
                return Reject(TEXT("native_mob_save_position"),Type,Id);
            Packet->SetNumberField(Axes[I],Coordinate-SourceOrigin[I]);
        }
        // Initial Fabric snapshots contain identity/pose/health only. All other
        // values come from their captured species profile, not hardcoded zombies.
        auto DefaultNumber=[&](const TCHAR* Key,double Default) {if(!Packet->HasField(Key)) Packet->SetNumberField(Key,Default);};
        DefaultNumber(TEXT("width"),Appearance->Width);DefaultNumber(TEXT("height"),Appearance->Height);
        DefaultNumber(TEXT("maxHealth"),Appearance->MaxHealth);DefaultNumber(TEXT("health"),Appearance->MaxHealth);
        DefaultNumber(TEXT("knockbackResistance"),Appearance->KnockbackResistance);DefaultNumber(TEXT("speed"),Appearance->Speed);DefaultNumber(TEXT("damage"),Appearance->Damage);DefaultNumber(TEXT("yaw"),0);
        if(!Packet->HasField(TEXT("hostile"))) Packet->SetBoolField(TEXT("hostile"),Appearance->Hostile);
        if(!Packet->HasField(TEXT("baby"))) Packet->SetBoolField(TEXT("baby"),Appearance->Baby);
        FBridgePacket Decoded;
        if(!BridgeProtocol::Parse(Packet,Decoded)) return Reject(TEXT("native_mob_save_validation"),Type,Id);
        double NativePitch=0;
        if(Json->HasField(TEXT("pitch")) && (!Json->TryGetNumberField(TEXT("pitch"),NativePitch) || !FMath::IsFinite(NativePitch) || FMath::Abs(NativePitch)>90))
            return Reject(TEXT("native_mob_save_pitch"),Type,Id);
        Parsed.Add(Decoded.Mob);Pitch.Add(float(NativePitch));UniqueIds.Add(Id);
    }
    // Malformed files never clear a running population. Empty arrays are valid
    // and intentionally clear all source mobs after the last one was killed.
    const float SavedPlayerHealth=PlayerHealth;
    Clear();PlayerHealth=SavedPlayerHealth;
    bool Complete=true;
    for(int32 I=0;I<Parsed.Num();++I) {
        if(!Import(Parsed[I],Anchor)) {
            if(Parsed[I].Type==TEXT("minecraft:bat") && LastReason.StartsWith(TEXT("bat restore blocked:"))) {
                FPendingRestore Pending;Pending.Snapshot=Parsed[I];Pending.Anchor=Anchor;Pending.Pitch=Pitch[I];Pending.SavedValue=Snapshots[I];
                PendingRestores.Add(MoveTemp(Pending));
                UE_LOG(LogTemp,Warning,TEXT("Bridge native mob deferred: id=%s type=%s position=%s retained in save; retry every 5 seconds"),*Parsed[I].Id,*Parsed[I].Type,*Parsed[I].Position.ToString());
            } else Complete=false;
            continue;
        }
        ABridgeMobCharacter* Mob=Mobs.Last();
        // Import already restored yaw and selected a supported, unblocked
        // location. Keep that safety correction if the player occupies the
        // original saved pose or the terrain has since changed.
        Mob->SetNativeViewPitch(Pitch[I]);
    }
    if(Complete) {
        UE_LOG(LogTemp,Display,TEXT("Bridge native mobs restored: requested=%d alive=%d pending=%d complete=true"),Snapshots.Num(),AliveCount(),PendingRestores.Num());
    } else {
        UE_LOG(LogTemp,Warning,TEXT("Bridge native mobs restored: requested=%d alive=%d complete=false"),Snapshots.Num(),AliveCount());
    }
    return Complete;
}
