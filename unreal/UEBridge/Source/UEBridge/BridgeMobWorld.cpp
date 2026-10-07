#include "BridgeMobWorld.h"
#include "BridgeMobCharacter.h"
#include "BridgeProtocol.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

ABridgeMobWorld::ABridgeMobWorld() { PrimaryActorTick.bCanEverTick=false; }

bool ABridgeMobWorld::Import(const FBridgeMobSnapshot& Snapshot,const FVector& Anchor) {
    if(SeenIds.Contains(Snapshot.Id)) return true; // A retried event must never resurrect a killed UE copy.
    if(SeenIds.Num()>=128 || !Palette || Snapshot.Id.IsEmpty()) { Rejected++;LastReason=Palette ? TEXT("mob_limit") : TEXT("missing_palette");return false; }
    const FBridgeMobAppearance* Appearance=Palette->Find(Snapshot.Appearance);
    if(!Appearance || Appearance->Type!=Snapshot.Type || !Appearance->Material) { MissingAppearance++;LastReason=TEXT("missing_appearance");return false; }
    FVector Feet=BridgeProtocol::ToUnreal(Snapshot.Position,Anchor);
    FVector Position=Feet+FVector(0,0,Snapshot.Height*50.f);
    FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABridgeMobCharacter* Mob=GetWorld()->SpawnActor<ABridgeMobCharacter>(Position,BridgeProtocol::ToRotation(Snapshot.Yaw,0),Params);
    if(!Mob || !Mob->Initialize(Snapshot,*Appearance,this)) { if(Mob) Mob->Destroy();Rejected++;LastReason=TEXT("invalid_model");return false; }
    Mob->SetAuthority(Authority,Player.Get());Mobs.Add(Mob);SeenIds.Add(Snapshot.Id);Imported++;LastReason=TEXT("ready");return true;
}

FString ABridgeMobWorld::SpawnEgg(const FString& Type,const FVector& Feet,const FVector& Anchor,const FString& Id) {
    if(!Authority || !Palette) return TEXT("mob palette not ready");
    const FString* Key=Palette->Templates.Find(Type);const FBridgeMobAppearance* Appearance=Key ? Palette->Find(*Key) : nullptr;
    if(!Appearance) return TEXT("mob template missing: ")+Type+TEXT("; export/import mobs again; flying/water species excluded");
    if(AliveCount()>=128 || SeenIds.Num()>=128) return TEXT("mob limit reached (128 per import)");
    FBridgeMobSnapshot Snapshot;Snapshot.Id=Id;Snapshot.Type=Type;Snapshot.Appearance=*Key;
    const FVector Relative=(Feet-Anchor)/100;Snapshot.Position=FVector(-Relative.Y,Relative.Z,Relative.X);
    Snapshot.Width=Appearance->Width;Snapshot.Height=Appearance->Height;Snapshot.MaxHealth=Appearance->MaxHealth;Snapshot.Health=Snapshot.MaxHealth;
    Snapshot.Speed=Appearance->Speed;Snapshot.Damage=Appearance->Damage;Snapshot.Hostile=Appearance->Hostile;Snapshot.Baby=Appearance->Baby;
    const float Half=Snapshot.Height*50.f,Radius=FMath::Min(Half,Snapshot.Width*50.f);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeEggSpawn),false,this);
    if(GetWorld()->OverlapBlockingTestByChannel(Feet+FVector(0,0,Half+2),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)) return TEXT("mob spawn blocked");
    Snapshot.Position.Y+=.02;
    return Import(Snapshot,Anchor) ? TEXT("mob spawned: ")+Type : LastReason;
}

void ABridgeMobWorld::SetAuthority(bool Active,ACharacter* NewPlayer) {
    Authority=Active;Player=NewPlayer;
    for(ABridgeMobCharacter* Mob:Mobs) if(IsValid(Mob)) Mob->SetAuthority(Active,NewPlayer);
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
void ABridgeMobWorld::RespawnPlayer() { PlayerHealth=20;LastPlayerDamage=GetWorld() ? GetWorld()->GetTimeSeconds()+1.f : -1; }
