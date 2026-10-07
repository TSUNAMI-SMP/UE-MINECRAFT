#include "BridgeItemWorld.h"
#include "BridgeDroppedItem.h"
#include "BridgeBlockPalette.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

ABridgeItemWorld::ABridgeItemWorld() {PrimaryActorTick.bCanEverTick=true;}
FString ABridgeItemWorld::Drop(const FString& Tx,const FString& Item,const FString& Model,int32 Count,int32 MaxCount,const FVector& Position,const FVector& Velocity) {
    if(const FEntry* Existing=Entries.Find(Tx)) return Existing->Rejected ? Existing->Reason : TEXT("already_spawned");
    if(Entries.Num()>=512) {if(Result) Result(Tx,0,TEXT("rejected"),Count,TEXT("item_transaction_limit"));return LastReason=TEXT("item_transaction_limit");}
    auto& Entry=Entries.Add(Tx);Entry.Item=Item;Entry.Model=Model;Entry.Count=Count;Entry.MaxCount=MaxCount;
    auto Reject=[&](const FString& Reason) {Entry.Rejected=true;Entry.Action=TEXT("rejected");Entry.Reason=Reason;return LastReason=Reason;};
    if(Count<1 || Count>99 || MaxCount<Count || MaxCount>99) return Reject(TEXT("item_quantity_invalid"));
    if(!Authority || !GetWorld()) return Reject(TEXT("item_controller_not_ready"));
    if(!Palette) return Reject(TEXT("item_palette_missing"));
    if(Contains && !Contains(Position)) return Reject(TEXT("item_outside_ready_import"));
    if(AliveCount()>=128) return Reject(TEXT("item_actor_limit"));
    TArray<FBridgeModelFace> Faces;
    if(!Palette->BuildItem(Model,TEXT("ground"),Faces) || Faces.IsEmpty()) return Reject(TEXT("item_ground_model_missing; /uebridge items export then import"));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeItemDrop),false,Player.Get());Query.AddIgnoredActor(this);
    FVector Spawn=Position;
    // Reject genuinely blocked positions, rather than spawning hidden items inside solid voxels.
    if(GetWorld()->OverlapBlockingTestByChannel(Spawn,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(12.5f),Query)) {
        const FVector TowardPlayer=Player.IsValid() ? (Player->GetActorLocation()-Spawn).GetSafeNormal() : FVector::UpVector;
        Spawn+=TowardPlayer*35.f;
        if(GetWorld()->OverlapBlockingTestByChannel(Spawn,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(12.5f),Query)) return Reject(TEXT("item_drop_position_blocked"));
    }
    if(Contains && !Contains(Spawn)) return Reject(TEXT("item_outside_ready_import"));
    FActorSpawnParameters Parameters;Parameters.Owner=this;Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Actor=GetWorld()->SpawnActor<ABridgeDroppedItem>(Spawn,FRotator::ZeroRotator,Parameters);
    if(!Actor || !Actor->Initialize(Palette,Model,Count,Velocity)) {if(Actor) Actor->Destroy();return Reject(TEXT("item_ground_model_invalid"));}
    Actor->Contains=Contains;Actor->AddTickPrerequisiteActor(this);Actor->SetActive(Authority);Actors.Add(Actor);Entry.Actor=Actor;Entry.Reason=TEXT("item_spawned");
    if(Lighting) Lighting(Actor);return LastReason=TEXT("item_spawned");
}
void ABridgeItemWorld::SetAuthority(bool Active,ACharacter* NewPlayer) {
    Authority=Active;Player=NewPlayer;for(auto* Actor:Actors) if(IsValid(Actor)) Actor->SetActive(Active);
}
bool ABridgeItemWorld::Resolve(const FString& Tx,int32 Revision,int32 Accepted) {
    FEntry* Entry=Entries.Find(Tx);if(!Entry || !Entry->Pending || Entry->Revision!=Revision || Accepted<0 || Accepted>99) return false;
    if(Revision==0) {
        if(Entry->Rejected ? Accepted>Entry->Count : Accepted!=0) return false;
        Entry->Pending=false;Entry->Spawned=!Entry->Rejected;return true;
    }
    if(Entry->Action==TEXT("pickup")) {
        if(Accepted>Entry->Requested || Accepted>Entry->Count) return false;
        Entry->Count-=Accepted;Entry->Pending=false;Entry->NextPickup=GetWorld()->GetTimeSeconds()+.5;
        auto* Actor=Entry->Actor.Get();if(Accepted>0 && Actor && PickupSound) PickupSound(Actor->GetActorLocation());Refresh(Actor);return true;
    }
    if(Entry->Action==TEXT("expired") && Accepted==0) {Entry->Count=0;Entry->Pending=false;Refresh(Entry->Actor.Get());return true;}
    return false;
}
void ABridgeItemWorld::Refresh(ABridgeDroppedItem* Actor) {
    if(!IsValid(Actor)) return;int32 Quantity=0;
    for(const auto& Pair:Entries) if(Pair.Value.Actor.Get()==Actor) Quantity+=Pair.Value.Count;
    if(Quantity<=0) {Actor->Destroy();Actors.Remove(Actor);} else Actor->SetQuantity(Quantity);
}
void ABridgeItemWorld::Merge() {
    for(int32 A=0;A<Actors.Num();++A) {
        ABridgeDroppedItem* Target=Actors[A];if(!IsValid(Target)) continue;
        for(int32 B=Actors.Num()-1;B>A;--B) {
            ABridgeDroppedItem* Source=Actors[B];if(!IsValid(Source) || Source->GetModelKey()!=Target->GetModelKey()
                || FVector::DistSquared(Source->GetActorLocation(),Target->GetActorLocation())>FMath::Square(50.f)) continue;
            bool Ready=true;int32 Maximum=99;
            for(const auto& Pair:Entries) if(Pair.Value.Actor.Get()==Target || Pair.Value.Actor.Get()==Source) {
                Ready&=Pair.Value.Spawned && !Pair.Value.Pending;Maximum=FMath::Min(Maximum,Pair.Value.MaxCount);
            }
            if(!Ready || Target->GetQuantity()+Source->GetQuantity()>Maximum) continue;
            for(auto& Pair:Entries) if(Pair.Value.Actor.Get()==Source) Pair.Value.Actor=Target;
            Target->MergeAge(Source->GetAge());Source->Destroy();Actors.RemoveAt(B);Refresh(Target);
        }
    }
}
void ABridgeItemWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);if(!GetWorld()) return;const double Now=GetWorld()->GetTimeSeconds();
    if(Authority && Lighting) for(auto* Actor:Actors) if(IsValid(Actor)) Lighting(Actor);
    const FVector PlayerCenter=Player.IsValid() ? Player->GetActorLocation() : FVector::ZeroVector;
    for(auto& Pair:Entries) {
        auto& Entry=Pair.Value;auto* Actor=Entry.Actor.Get();
        if(Authority && Actor && Entry.Spawned && !Entry.Pending && Entry.Count>0) {
            if(Actor->GetAge()>=300.f || Actor->GetActorLocation().Z<-100000.f) {
                Entry.Action=TEXT("expired");Entry.Requested=Entry.Count;Entry.Pending=true;Entry.Revision++;Entry.Reason=TEXT("item_vanilla_despawn_300s");Entry.LastSent=-1;
            } else if(Player.IsValid() && Actor->GetAge()>=.5f && Now>=Entry.NextPickup) {
                const float Half=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                FVector Nearest=PlayerCenter;Nearest.Z=FMath::Clamp(Actor->GetActorLocation().Z,PlayerCenter.Z-Half,PlayerCenter.Z+Half);
                if(FVector::DistSquared(Actor->GetActorLocation(),Nearest)<FMath::Square(85.f)) {
                    Entry.Action=TEXT("pickup");Entry.Requested=Entry.Count;Entry.Pending=true;Entry.Revision++;Entry.Reason=TEXT("item_pickup");Entry.LastSent=-1;
                }
            }
        }
        if(Entry.Pending && Result && (Entry.LastSent<0 || Now-Entry.LastSent>=.1)) {
            Result(Pair.Key,Entry.Revision,Entry.Action,Entry.Revision==0 ? Entry.Count : Entry.Requested,Entry.Reason);Entry.LastSent=Now;
        }
    }
    if(Authority && Now>=MergeTime) {MergeTime=Now+.5;Merge();}
}
int32 ABridgeItemWorld::AliveCount() const {int32 Count=0;for(auto* Actor:Actors) if(IsValid(Actor)) Count++;return Count;}
TArray<FVector> ABridgeItemWorld::CollisionAnchors() const {TArray<FVector> Points;for(auto* Actor:Actors) if(IsValid(Actor)) Points.Add(Actor->GetActorLocation()-FVector(0,0,12.5f));return Points;}
void ABridgeItemWorld::Clear() {for(auto* Actor:Actors) if(IsValid(Actor)) Actor->Destroy();Actors.Empty();Entries.Empty();Authority=false;Player.Reset();LastReason=TEXT("item_escrow_released");}
void ABridgeItemWorld::EndPlay(const EEndPlayReason::Type Reason) {Clear();Super::EndPlay(Reason);}
