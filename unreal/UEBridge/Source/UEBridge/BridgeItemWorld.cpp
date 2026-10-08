#include "BridgeItemWorld.h"
#include "BridgeDroppedItem.h"
#include "BridgeBlockPalette.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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
void ABridgeItemWorld::SetPickupDelay(const FString& Tx,float Seconds) {if(auto* Entry=Entries.Find(Tx)) if(auto* Actor=Entry->Actor.Get()) Actor->SetPickupDelay(Seconds);}
void ABridgeItemWorld::SetAuthority(bool Active,ACharacter* NewPlayer) {
    Authority=Active;Player=NewPlayer;for(ABridgeDroppedItem* Actor:Actors) if(IsValid(Actor)) Actor->SetActive(Active);
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
            bool Ready=true;int32 Maximum=99;FString ItemId;
            for(const auto& Pair:Entries) if(Pair.Value.Actor.Get()==Target || Pair.Value.Actor.Get()==Source) {
                Ready&=Pair.Value.Spawned && !Pair.Value.Pending;Maximum=FMath::Min(Maximum,Pair.Value.MaxCount);
                if(ItemId.IsEmpty()) ItemId=Pair.Value.Item;else Ready&=ItemId==Pair.Value.Item;
            }
            if(!Ready || Target->GetQuantity()+Source->GetQuantity()>Maximum) continue;
            for(auto& Pair:Entries) if(Pair.Value.Actor.Get()==Source) Pair.Value.Actor=Target;
            Target->MergeAge(Source->GetAge());Source->Destroy();Actors.RemoveAt(B);Refresh(Target);
        }
    }
}
void ABridgeItemWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);if(!GetWorld()) return;const double Now=GetWorld()->GetTimeSeconds();
    if(Authority && Lighting) for(ABridgeDroppedItem* Actor:Actors) if(IsValid(Actor)) Lighting(Actor);
    const FVector PlayerCenter=Player.IsValid() ? Player->GetActorLocation() : FVector::ZeroVector;
    for(auto& Pair:Entries) {
        auto& Entry=Pair.Value;auto* Actor=Entry.Actor.Get();
        if(Authority && Actor && Entry.Spawned && !Entry.Pending && Entry.Count>0) {
            if(Actor->GetAge()>=300.f || Actor->GetActorLocation().Z<-100000.f) {
                Entry.Action=TEXT("expired");Entry.Requested=Entry.Count;Entry.Pending=true;Entry.Revision++;Entry.Reason=TEXT("item_vanilla_despawn_300s");Entry.LastSent=-1;
            } else if(Player.IsValid() && Actor->CanPickup() && Now>=Entry.NextPickup) {
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
    // Result can resolve an entry synchronously while iterating above. Only
    // remove completed local entries after that iteration has ended; legacy
    // receipts retain their tombstones to reject delayed UDP retries.
    if(NativeLocal) for(auto It=Entries.CreateIterator();It;++It) if(!It.Value().Pending && (It.Value().Rejected || It.Value().Count<=0)) It.RemoveCurrent();
}
TSet<FString> ABridgeItemWorld::GetTransactionIds() const {TSet<FString> Ids;for(const auto& Pair:Entries) Ids.Add(Pair.Key);return Ids;}
int32 ABridgeItemWorld::AliveCount() const {int32 Count=0;for(ABridgeDroppedItem* Actor:Actors) if(IsValid(Actor)) Count++;return Count;}
TArray<FVector> ABridgeItemWorld::CollisionAnchors() const {TArray<FVector> Points;for(ABridgeDroppedItem* Actor:Actors) if(IsValid(Actor)) Points.Add(Actor->GetActorLocation()-FVector(0,0,12.5f));return Points;}
void ABridgeItemWorld::Clear() {for(ABridgeDroppedItem* Actor:Actors) if(IsValid(Actor)) Actor->Destroy();Actors.Empty();Entries.Empty();Authority=false;Player.Reset();LastReason=TEXT("item_escrow_released");}
void ABridgeItemWorld::EndPlay(const EEndPlayReason::Type Reason) {Clear();Super::EndPlay(Reason);}

TArray<TSharedPtr<FJsonValue>> ABridgeItemWorld::ExportNativeDrops(const FVector& Anchor,const FVector& SourceOrigin) const {
    TArray<TSharedPtr<FJsonValue>> Saved;
    auto Vector=[](const FVector& Value) {return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};};
    for(const ABridgeDroppedItem* Actor:Actors) {
        if(!IsValid(Actor) || Actor->GetQuantity()<=0) continue;
        FString Tx,Item,Model;int32 Quantity=0,Maximum=99;
        for(const auto& Pair:Entries) if(Pair.Value.Actor.Get()==Actor && Pair.Value.Spawned && !Pair.Value.Rejected && Pair.Value.Count>0) {
            // A pending pickup is still present in the world until accepted.
            if(Tx.IsEmpty() || Pair.Key<Tx) Tx=Pair.Key;
            Item=Pair.Value.Item;Model=Pair.Value.Model;Quantity+=Pair.Value.Count;Maximum=FMath::Min(Maximum,Pair.Value.MaxCount);
        }
        if(Tx.IsEmpty() || Quantity<=0 || Quantity>Maximum) continue;
        const FVector Relative=(Actor->GetActorLocation()-Anchor)/100.;
        const FVector Absolute=SourceOrigin+FVector(-Relative.Y,Relative.Z,Relative.X);
        auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("id"),Tx);Json->SetStringField(TEXT("item"),Item);Json->SetStringField(TEXT("model"),Model);
        Json->SetNumberField(TEXT("count"),Quantity);Json->SetNumberField(TEXT("maxCount"),Maximum);Json->SetArrayField(TEXT("position"),Vector(Absolute));
        Json->SetArrayField(TEXT("velocity"),Vector(Actor->GetNativeVelocity()));Json->SetNumberField(TEXT("age"),FMath::Clamp(Actor->GetAge(),0.f,300.f));Json->SetNumberField(TEXT("pickupDelay"),Actor->GetPickupDelay());Saved.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Saved;
}

bool ABridgeItemWorld::ImportNativeDrops(const TArray<TSharedPtr<FJsonValue>>& Drops,const FVector& Anchor,const FVector& SourceOrigin,TMap<FString,FString>& OutTransactionItems) {
    auto Reject=[&](const FString& Reason) {LastReason=Reason;UE_LOG(LogTemp,Warning,TEXT("Bridge native drops restore failed: %s"),*Reason);return false;};
    if(Drops.Num()>128 || !Authority || !GetWorld()) return Reject(TEXT("native_drop_save_limit_or_controller_not_ready"));
    if(!Drops.IsEmpty() && !Palette) return Reject(TEXT("native_drop_palette_missing"));
    struct FDrop {FString Tx,Item,Model;int32 Count=0,Maximum=0;FVector Position,Velocity;float Age=0,PickupDelay=.5f;};
    TArray<FDrop> Parsed;TSet<FString> UniqueIds;
    auto Identifier=[](const FString& Id) {
        if(Id.Len()<3 || Id.Len()>256 || Id.Contains(TEXT(".."))) return false;
        int32 Colons=0;for(TCHAR C:Id) {if(C==':') {++Colons;continue;}if(!((C>='a' && C<='z') || (C>='0' && C<='9') || C=='_' || C=='/' || C=='.' || C=='-')) return false;}
        return Colons==1 && !Id.StartsWith(TEXT(":")) && !Id.EndsWith(TEXT(":"));
    };
    auto Number=[](const TSharedPtr<FJsonObject>& Json,const TCHAR* Key,double Low,double High,double& Value) {return Json->TryGetNumberField(Key,Value) && FMath::IsFinite(Value) && Value>=Low && Value<=High;};
    auto ReadVector=[](const TSharedPtr<FJsonObject>& Json,const TCHAR* Key,double Limit,FVector& Out) {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!Json->TryGetArrayField(Key,Values) || Values->Num()!=3) return false;
        for(int32 I=0;I<3;++I) {double Number=0;if(!(*Values)[I].IsValid() || !(*Values)[I]->TryGetNumber(Number) || !FMath::IsFinite(Number) || FMath::Abs(Number)>Limit) return false;Out[I]=Number;}
        return true;
    };
    for(const auto& Value:Drops) {
        if(!Value.IsValid() || Value->Type!=EJson::Object) return Reject(TEXT("native_drop_save_object"));
        const auto Json=Value->AsObject();FDrop DropData;double Count=0,Maximum=0,Age=0;FGuid Guid;FVector Absolute;
        if(!Json.IsValid() || !Json->TryGetStringField(TEXT("id"),DropData.Tx) || !FGuid::ParseExact(DropData.Tx,EGuidFormats::DigitsWithHyphens,Guid) || UniqueIds.Contains(DropData.Tx)
            || !Json->TryGetStringField(TEXT("item"),DropData.Item) || !Identifier(DropData.Item) || !Json->TryGetStringField(TEXT("model"),DropData.Model) || DropData.Model.IsEmpty() || DropData.Model.Len()>256
            || !Number(Json,TEXT("count"),1,99,Count) || FMath::FloorToDouble(Count)!=Count || !Number(Json,TEXT("maxCount"),1,99,Maximum) || FMath::FloorToDouble(Maximum)!=Maximum || Count>Maximum
            || !Number(Json,TEXT("age"),0,300,Age) || !ReadVector(Json,TEXT("position"),30000000,Absolute) || (Absolute-SourceOrigin).GetAbs().GetMax()>100000 || !ReadVector(Json,TEXT("velocity"),100000,DropData.Velocity)) return Reject(TEXT("native_drop_save_validation"));
        DropData.Position=Anchor+FVector(Absolute.Z-SourceOrigin.Z,-(Absolute.X-SourceOrigin.X),Absolute.Y-SourceOrigin.Y)*100.;
        DropData.Count=int32(Count);DropData.Maximum=int32(Maximum);DropData.Age=float(Age);double Delay=.5;if(Json->HasField(TEXT("pickupDelay")) && !Number(Json,TEXT("pickupDelay"),0,10,Delay)) return Reject(TEXT("native_drop_pickup_delay"));DropData.PickupDelay=float(Delay);
        if(Contains && !Contains(DropData.Position)) return Reject(TEXT("native_drop_outside_ready_import"));
        TArray<FBridgeModelFace> Faces;if(!Palette->BuildItem(DropData.Model,TEXT("ground"),Faces) || Faces.IsEmpty()) return Reject(TEXT("native_drop_ground_model_missing"));
        for(const auto& Face:Faces) {const auto* Material=Palette->ItemMaterials.Find(Face.TextureId);if(!Material || !Material->Get()) return Reject(TEXT("native_drop_ground_material_missing"));}
        UniqueIds.Add(DropData.Tx);Parsed.Add(MoveTemp(DropData));
    }
    // Stage against empty bookkeeping and preserve old actors until every new
    // model is initialized. A failed spawn rolls back the entire population.
    auto PreviousActors=MoveTemp(Actors);auto PreviousEntries=MoveTemp(Entries);TArray<ECollisionEnabled::Type> PreviousCollision;
    for(ABridgeDroppedItem* Actor:PreviousActors) {
        auto* Primitive=IsValid(Actor) ? Cast<UPrimitiveComponent>(Actor->GetRootComponent()) : nullptr;
        PreviousCollision.Add(Primitive ? Primitive->GetCollisionEnabled() : ECollisionEnabled::NoCollision);
        if(Primitive) Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    TMap<FString,FString> Restored;
    for(const auto& Data:Parsed) {
        const FString SpawnReason=Drop(Data.Tx,Data.Item,Data.Model,Data.Count,Data.Maximum,Data.Position,Data.Velocity);
        if(SpawnReason!=TEXT("item_spawned") || !Resolve(Data.Tx,0,0)) {
            for(ABridgeDroppedItem* Actor:Actors) if(IsValid(Actor)) Actor->Destroy();Actors=MoveTemp(PreviousActors);Entries=MoveTemp(PreviousEntries);
            for(int32 I=0;I<Actors.Num();++I) if(IsValid(Actors[I])) if(auto* Primitive=Cast<UPrimitiveComponent>(Actors[I]->GetRootComponent())) Primitive->SetCollisionEnabled(PreviousCollision[I]);
            return Reject(TEXT("native_drop_spawn_failed: ")+SpawnReason);
        }
        if(auto* Actor=Entries.FindChecked(Data.Tx).Actor.Get()) {Actor->RestoreNativeMotion(Data.Velocity,Data.Age);Actor->SetPickupDelay(Data.PickupDelay);}
        Restored.Add(Data.Tx,Data.Item);
    }
    for(ABridgeDroppedItem* Actor:PreviousActors) if(IsValid(Actor)) Actor->Destroy();OutTransactionItems=MoveTemp(Restored);LastReason=TEXT("native_drops_restored");
    UE_LOG(LogTemp,Display,TEXT("Bridge native drops restored: stacks=%d actors=%d complete=true"),Drops.Num(),AliveCount());return true;
}
