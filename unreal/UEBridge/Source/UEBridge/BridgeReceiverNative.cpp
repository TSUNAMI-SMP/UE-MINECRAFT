#include "BridgeReceiver.h"
#include "BridgeNativeWorldStore.h"
#include "BridgeNativePlayerController.h"
#include "BridgeNativeInventory.h"
#include "BridgeNativeUiPalette.h"
#include "BridgeNativeSoundPalette.h"
#include "BridgeWorld.h"
#include "BridgeCharacter.h"
#include "BridgePlayerAppearance.h"
#include "BridgeBlockPalette.h"
#include "BridgeMobWorld.h"
#include "BridgeItemWorld.h"
#include "BridgeVanillaEffects.h"
#include "BridgeLightingService.h"
#include "BridgeVideo.h"
#include "BridgeArrow.h"
#include "BridgeNativeExplosion.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"

namespace {
FVector MinecraftDelta(const FVector& UE) {return FVector(-UE.Y,UE.Z,UE.X)/100.;}
ABridgeNativePlayerController* Controller(const ABridgeReceiver* Receiver) {
    return Cast<ABridgeNativePlayerController>(UGameplayStatics::GetPlayerController(Receiver,0));
}
bool JsonVector(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,FVector& Value) {
    const TArray<TSharedPtr<FJsonValue>>* Array=nullptr;double X,Y,Z;
    if(!Object.IsValid() || !Object->TryGetArrayField(Key,Array) || Array->Num()!=3
        || !(*Array)[0]->TryGetNumber(X) || !(*Array)[1]->TryGetNumber(Y) || !(*Array)[2]->TryGetNumber(Z)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z) || FMath::Abs(X)>30000000 || FMath::Abs(Y)>30000000 || FMath::Abs(Z)>30000000) return false;
    Value=FVector(X,Y,Z);return true;
}
TArray<TSharedPtr<FJsonValue>> JsonVector(const FVector& Value) {
    return {MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};
}
}

bool ABridgeReceiver::IsNativeReady() const {
    return NativePlayActive && NativeInitialized && !NativeRestoreFailed && NativeStore.IsValid() && NativeStore->IsReady()
        && !NativeStore->IsSaving() && IsValid(SyncedWorld) && SyncedWorld->IsSealed() && TerrainMovementReady;
}
float ABridgeReceiver::GetNativeHealth() const {return IsValid(MobWorld) ? MobWorld->PlayerHealth : 20.f;}
bool ABridgeReceiver::IsNativeSaving() const {return NativePlayActive && NativeStore.IsValid() && NativeStore->IsSaving();}

void ABridgeReceiver::BeginNativePlay() {
    NativePlayActive=true;NativeInitialized=false;Connected=false;Anchor=FVector::ZeroVector;
    NativeTearDownHandle=FWorldDelegates::OnWorldBeginTearDown.AddUObject(this,&ABridgeReceiver::PrepareNativeExit);
    PrimaryActorTick.bTickEvenWhenPaused=true;
    NativeStore=MakeShared<FBridgeNativeWorldStore>();
    SyncedWorld=GetWorld()->SpawnActor<ABridgeWorld>();
    if(!SyncedWorld || !TexturePalette || !NativeUiPalette || !PlayerAppearance) {
        NativeStatus=TEXT("Setup incomplete: run import_native_play.py for this export package");
        UE_LOG(LogTemp,Error,TEXT("Bridge native setup failed: blocks=%s UI=%s player=%s mobs=%s; %s"),
            *GetNameSafe(TexturePalette),*GetNameSafe(NativeUiPalette),*GetNameSafe(PlayerAppearance),*GetNameSafe(MobPalette),*NativeStatus);
        return;
    }
    if(!NativeStore->BeginLoad(NativeWorldFile,SyncedWorld,Anchor,PreviewMaterial,TexturePalette)) {
        NativeStatus=NativeStore->GetError();return;
    }
    SourceOrigin=NativeStore->GetMetadata().Origin;
    NativeRespawnPosition=NativeStore->GetMetadata().Spawn;
    LatestInput=FBridgePacket();LatestInput.Controller=true;LatestInput.Creative=true;LatestInput.ItemSession=true;
    LatestInput.VanillaLight=NativeStore->GetMetadata().Environment;
    Session=NativeStore->GetMetadata().PackageId;
    if(const auto& Manifest=NativeStore->GetMetadata().Manifest; Manifest.IsValid()) {
        const TSharedPtr<FJsonObject>* Settings=nullptr;
        if(Manifest->TryGetObjectField(TEXT("settings"),Settings)) {
            FString Mode;(*Settings)->TryGetStringField(TEXT("gameMode"),Mode);NativeCreative=Mode!=TEXT("survival");
            double Volume;if((*Settings)->TryGetNumberField(TEXT("soundMasterVolume"),Volume)) NativeMasterVolume=FMath::Clamp(float(Volume),0.f,1.f);
            const TSharedPtr<FJsonObject>* Volumes=nullptr;
            if((*Settings)->TryGetObjectField(TEXT("soundVolumes"),Volumes)) for(const auto& Pair:(*Volumes)->Values) {
                double Value;if(Pair.Value->TryGetNumber(Value)) NativeSoundVolumes.Add(FString(*Pair.Key),FMath::Clamp(float(Value),0.f,1.f));
            }
            (*Settings)->SetStringField(TEXT("profile"),Session);
            if(auto* PC=Controller(this)) {PC->ConfigureNativeSettings(*Settings);NativeControllerConfigured=true;}
            if(EnsureMobWorld()) {double Health;if((*Settings)->TryGetNumberField(TEXT("health"),Health)) MobWorld->PlayerHealth=FMath::Clamp(float(Health),0.f,20.f);}
        }
    }
    LatestInput.Creative=NativeCreative;
    if(const auto& Runtime=NativeStore->GetMetadata().RuntimeState; Runtime.IsValid()) {
        Runtime->TryGetBoolField(TEXT("lighting"),NativeLighting);
        FVector Respawn;if(JsonVector(Runtime,TEXT("respawn"),Respawn)) NativeRespawnPosition=Respawn;
        if(EnsureMobWorld()) {double Health;if(Runtime->TryGetNumberField(TEXT("health"),Health)) MobWorld->PlayerHealth=FMath::Clamp(float(Health),0.f,20.f);}
    }
    VanillaEffects=GetWorld()->SpawnActor<ABridgeVanillaEffects>();
    if(VanillaEffects) {
        VanillaEffects->SampleLight=[this](const FVector& Position) {
            if(!SyncedWorld || !SyncedWorld->GetLighting()) return FLinearColor(1,0,1,1);
            const FVector Source=SourceOrigin+MinecraftDelta(Position-Anchor);
            return SyncedWorld->GetLighting()->Sample(FIntVector(FMath::FloorToInt(Source.X),FMath::FloorToInt(Source.Y),FMath::FloorToInt(Source.Z)));
        };
    }
    NativeSoundAttenuation=NewObject<USoundAttenuation>(this);
    NativeSoundAttenuation->Attenuation.bAttenuate=true;
    NativeSoundAttenuation->Attenuation.bSpatialize=true;
    NativeSoundAttenuation->Attenuation.AttenuationShapeExtents=FVector(100,0,0);
    NativeSoundAttenuation->Attenuation.FalloffDistance=1500;
    Video->SetMinecraftOrigin(SourceOrigin,Anchor);
    Video->SetNativeSkyPalette(NativeUiPalette);
    Video->SetNativeSkyEnvironment(LatestInput.VanillaLight);
    NativeSetLighting(NativeLighting);
    NativeStatus=TEXT("Validating offline world...");
    UE_LOG(LogTemp,Display,TEXT("Bridge 0.12.1 native start: package=%s file=%s input=UE render=UE videoTransfer=bypassed"),*Session,*NativeWorldFile);
}

void ABridgeReceiver::TickNativePlay(float DeltaSeconds) {
    const double Now=FPlatformTime::Seconds();
    if(!NativeStore.IsValid()) return;
    if(!Video->IsNativeRenderModeActive()) Video->SetNativeRenderMode(NativeLighting);
    if(auto* PC=Controller(this)) if(!PC->IsSavedInventoryValid()) {NativeRestoreFailed=true;NativeStatus=PC->GetInventoryRestoreError();}
    if(NativeRestoreFailed) {if(auto* Pawn=Cast<ABridgeCharacter>(TargetCharacter)) Pawn->SetAuthorityEnabled(false);LogDiagnostics(Now);return;}
    NativeStore->Tick(4);
    if(NativeSavePausedWorld && !NativeStore->IsSaving()) {
        NativeSavePausedWorld=false;
        if(auto* PC=Controller(this)) UGameplayStatics::SetGamePaused(this,PC->IsPausedMenuOpen());
    }
    auto* Character=Cast<ABridgeCharacter>(TargetCharacter);
    if(!Character) {NativeStatus=TEXT("Waiting for BridgeCharacter pawn...");return;}
    if(!NativeStore->GetError().IsEmpty() && !NativeStore->IsReady()) {
        NativeStatus=NativeStore->GetError();Character->SetAuthorityEnabled(false);LogDiagnostics(Now);return;
    }
    if(NativeStore->IsLoading()) {
        NativeStatus=FString::Printf(TEXT("%s %.0f%%"),*NativeStore->GetState(),NativeStore->GetProgress()*100);
        Character->SetAuthorityEnabled(false);LogDiagnostics(Now);return;
    }
    if(!NativeStore->IsReady()) return;
    auto* PC=Controller(this);
    if(!PC) {NativeStatus=TEXT("Waiting for native player controller...");return;}
    if(!NativeControllerConfigured) {
        TSharedPtr<FJsonObject> Settings=MakeShared<FJsonObject>();
        const auto& Manifest=NativeStore->GetMetadata().Manifest;const TSharedPtr<FJsonObject>* Imported=nullptr;
        if(Manifest.IsValid() && Manifest->TryGetObjectField(TEXT("settings"),Imported)) Settings=*Imported;
        Settings->SetStringField(TEXT("profile"),Session);PC->ConfigureNativeSettings(Settings);NativeControllerConfigured=true;
    }
    if(!NativeInitialized) {
        const auto& Metadata=NativeStore->GetMetadata();
        SourceOrigin=Metadata.Origin;LatestInput.VanillaLight=Metadata.Environment;
        Character->ConfigureAppearance(PlayerAppearance);Character->ConfigureOutline(OutlineMaterial);Character->SetInteractionWorld(SyncedWorld);
        const FVector Feet=BridgeProtocol::ToUnreal(Metadata.Spawn-SourceOrigin,Anchor);
        SyncedWorld->EnsureCollisionForPosition(Feet);
        Character->SetActorLocation(Feet+FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),false,nullptr,ETeleportType::TeleportPhysics);
        bool Flying=false;int32 Perspective=LatestInput.Perspective;double Number;
        if(Metadata.Manifest.IsValid()) {
            const TSharedPtr<FJsonObject>* Settings=nullptr;
            if(Metadata.Manifest->TryGetObjectField(TEXT("settings"),Settings)) {
                (*Settings)->TryGetBoolField(TEXT("flying"),Flying);
                if((*Settings)->TryGetNumberField(TEXT("perspective"),Number)) Perspective=FMath::Clamp(int32(Number),0,2);
            }
        }
        if(Metadata.RuntimeState.IsValid()) {
            Metadata.RuntimeState->TryGetBoolField(TEXT("flying"),Flying);
            if(Metadata.RuntimeState->TryGetNumberField(TEXT("perspective"),Number)) Perspective=FMath::Clamp(int32(Number),0,2);
            const TSharedPtr<FJsonObject>* Inventory=nullptr;
            if(Metadata.RuntimeState->TryGetObjectField(TEXT("inventory"),Inventory) && !PC->RestoreNativeInventory(*Inventory)) {
                NativeRestoreFailed=true;NativeStatus=TEXT("Saved inventory is invalid; source and save were preserved");return;
            }
        }
        PC->RestoreNativeView(Metadata.Yaw,-Metadata.Pitch,NativeCreative && Flying,Perspective);
        LatestInput.Flying=NativeCreative && Flying;LatestInput.Perspective=Perspective;
        if(EnsureMobWorld() && !MobWorld->ImportNativeSnapshots(Metadata.Mobs,Anchor,SourceOrigin)) {
            NativeRestoreFailed=true;NativeStatus=TEXT("Mob restore failed; see UEBridge.log");UE_LOG(LogTemp,Error,TEXT("Bridge native: mob restore failed"));return;
        }
        if(Metadata.RuntimeState.IsValid()) {
            const TArray<TSharedPtr<FJsonValue>>* Drops=nullptr;
            if(Metadata.RuntimeState->TryGetArrayField(TEXT("drops"),Drops)) {
                if(!EnsureItemWorld()) {NativeRestoreFailed=true;NativeStatus=TEXT("Dropped-item world unavailable");return;}
                for(const auto& Value:*Drops) {
                    if(!Value.IsValid() || Value->Type!=EJson::Object) continue;FVector Position;
                    if(JsonVector(Value->AsObject(),TEXT("position"),Position)) SyncedWorld->EnsureCollisionForPosition(BridgeProtocol::ToUnreal(Position-SourceOrigin,Anchor));
                }
                ItemWorld->SetAuthority(true,Character);
                if(!ItemWorld->ImportNativeDrops(*Drops,Anchor,SourceOrigin,NativeDropItems)) {
                    NativeRestoreFailed=true;NativeStatus=TEXT("Saved dropped items are invalid; save preserved");return;
                }
            }
            const TArray<TSharedPtr<FJsonValue>>* Fuses=nullptr;
            if(Metadata.RuntimeState->TryGetArrayField(TEXT("fuses"),Fuses)) {
                if(Fuses->Num()>64) {NativeRestoreFailed=true;NativeStatus=TEXT("Saved TNT exceeds limit");return;}
                for(const auto& Value:*Fuses) {
                    FVector Position;double Remaining;
                    if(!Value.IsValid() || Value->Type!=EJson::Object || !JsonVector(Value->AsObject(),TEXT("position"),Position)
                        || !Value->AsObject()->TryGetNumberField(TEXT("remainingSeconds"),Remaining) || !FMath::IsFinite(Remaining) || Remaining<0 || Remaining>4
                        || !SyncedWorld->ContainsUEPosition(BridgeProtocol::ToUnreal(Position-SourceOrigin,Anchor))) {
                        NativeRestoreFailed=true;NativeStatus=TEXT("Saved TNT is invalid; save preserved");return;
                    }
                    NativeFuses.Add({BridgeProtocol::ToUnreal(Position-SourceOrigin,Anchor),GetWorld()->GetTimeSeconds()+Remaining});
                }
            }
        }
        NativeInitialized=true;NativeLastAutosave=Now;
        UE_LOG(LogTemp,Display,TEXT("Bridge native terrain loaded: package=%s cells=%d spawn=%s save=%s"),
            *Metadata.PackageId,Metadata.Cells,*Metadata.Spawn.ToString(),*Metadata.SaveFile);
    }
    TerrainMovementReady=SyncedWorld->IsMovementReady(Character->GetMinecraftFeetPosition(),Character->GetVelocity());
    const bool Running=!NativeStore->IsSaving() && GetNativeHealth()>0 && TerrainMovementReady;
    UEControl=Running;Character->SetAuthorityEnabled(Running);
    Character->ConfigureVisuals(PreviewMaterial,TexturePalette,LatestInput.HeldItem,LatestInput.HeldBlock,LatestInput.HeldColor,LatestInput.HeldModelKey);
    if(Running && !GetWorld()->IsPaused()) {
        Character->ApplyFlight(NativeCreative,LatestInput.Flying);
        Character->ApplyUEInput(ForwardInput,RightInput,JumpHeld,SneakHeld,LatestInput.Sprint);
    }
    if(NativeBowStart>=0) Character->SetNativeUse(true,FMath::Clamp(float(GetWorld()->GetTimeSeconds()-NativeBowStart),0.f,1.f));
    if(VanillaEffects) {
        VanillaEffects->Configure(SyncedWorld,TexturePalette,VanillaParticleMaterial);VanillaEffects->SetViewCamera(Character->BridgeCamera);
        VanillaEffects->AddTickPrerequisiteActor(Character);VanillaEffects->AddTickPrerequisiteComponent(Character->GetCharacterMovement());
        if(Running && !GetWorld()->IsPaused()) {
            TArray<FBridgeVanillaEvent> Events;VanillaEffects->SampleCharacter(Character,DeltaSeconds,Events);
            for(const auto& Event:Events) QueueFeedback(Event.Type,Event.BlockId,Event.Position,Event.FallDistance);
        } else VanillaEffects->ResetMovement();
    }
    if(MobWorld) {MobWorld->Palette=MobPalette;MobWorld->SetCreative(NativeCreative);MobWorld->SetAuthority(Running,Character);}
    if(ItemWorld) ItemWorld->SetAuthority(Running,Character);
    SyncedWorld->InteractionSound=[this](const FString& Type,const FString& Block,const FVector& Position){QueueFeedback(Type,Block,Position);};
    TArray<FVector> Extra;if(MobWorld) Extra.Append(MobWorld->CollisionAnchors());if(ItemWorld) Extra.Append(ItemWorld->CollisionAnchors());
    SyncedWorld->UpdateCollisionCenters(Character->GetMinecraftFeetPosition(),Extra);
    if(LastLightActors<0 || Now-LastLightActors>=.1) {
        LastLightActors=Now;
        FBridgeLightingService::SetEnvironment(GetWorld(),LatestInput.VanillaLight,!NativeLighting);
        for(TActorIterator<AActor> It(GetWorld());It;++It) if(It->ActorHasTag(TEXT("BridgeMinecraftVisual"))) LightActor(*It);
        LightActor(Character);
    }
    if(!NativeStore->IsSaving() && !GetWorld()->IsPaused()) {
        for(int32 I=NativeFuses.Num()-1;I>=0;--I) if(GetWorld()->GetTimeSeconds()>=NativeFuses[I].Deadline) {
            const FVector Position=NativeFuses[I].Position;NativeFuses.RemoveAtSwap(I);
            if(!ExplosionSystem && !ABridgeNativeExplosion::Spawn(GetWorld(),Position,NativeUiPalette))
                UE_LOG(LogTemp,Warning,TEXT("Bridge native explosion sprite unavailable; rerun native import"));
            Explode(Position);RemoveImportedBlocks(Position,ExplosionRadius);
            PlayNativeSound(TEXT("minecraft:entity.generic.explode"),Position,4,1,TEXT("block"));
        }
    }
    NativeStatus=NativeStore->IsSaving() ? FString::Printf(TEXT("Saving %.0f%%"),NativeStore->GetProgress()*100)
        : GetNativeHealth()<=0 ? TEXT("You died") : !TerrainMovementReady ? TEXT("Preparing terrain collision...")
        : !NativeStore->GetError().IsEmpty() ? TEXT("Save failed: ")+NativeStore->GetError() : TEXT("Ready");
    PerformanceSeconds+=DeltaSeconds;++PerformanceFrames;LogDiagnostics(Now);
    if(!GetWorld()->IsPaused() && !NativeStore->IsSaving() && Now-NativeLastAutosave>=60) {NativeLastAutosave=Now;NativeSave();}
}

void ABridgeReceiver::SetNativeInput(float Forward,float Right,bool Jump,bool Sneak,bool Sprint,bool Flying,int32 Perspective,float UEYaw,float UEPitch) {
    if(!NativePlayActive) return;
    ForwardInput=FMath::Clamp(Forward,-1.f,1.f);RightInput=FMath::Clamp(Right,-1.f,1.f);JumpHeld=Jump;SneakHeld=Sneak;
    LatestInput.Forward=ForwardInput;LatestInput.Right=RightInput;LatestInput.Jump=Jump;LatestInput.Sneak=Sneak;
    LatestInput.Sprint=Sprint;LatestInput.Flying=NativeCreative && Flying;LatestInput.Perspective=FMath::Clamp(Perspective,0,2);
    LatestInput.Yaw=UEYaw;LatestInput.Pitch=-UEPitch;LatestInput.Creative=NativeCreative;
    ++LastSequence;LastInput=FPlatformTime::Seconds();
}
void ABridgeReceiver::NativeSelect(const FString& ItemId) {
    if(!NativePlayActive) return;
    LatestInput.HeldItem=ItemId;LatestInput.HeldBlock.Empty();LatestInput.HeldModelKey.Empty();LatestInput.SpawnType.Empty();
    LatestInput.HeldColor=0xffffff;
    if(const auto* Item=NativeUiPalette ? NativeUiPalette->FindItem(ItemId) : nullptr) {
        LatestInput.HeldBlock=Item->BlockId;LatestInput.HeldModelKey=Item->ModelKey;
        if(TexturePalette && !Item->BlockId.IsEmpty()) LatestInput.HeldColor=TexturePalette->ParticleTint(Item->BlockId).ToPackedARGB() & 0xffffffu;
        LatestInput.SpawnType=Item->SpawnType;
    }
}
void ABridgeReceiver::NativeAction(const FString& Action) {
    if(NativePlayActive && Action==TEXT("ui_click")) {PlayNativeSound(TEXT("minecraft:ui.button.click"),FVector::ZeroVector,1,1,TEXT("master"));return;}
    if(Action==TEXT("use_cancel")) {
        NativeBowStart=-1;if(auto* Character=Cast<ABridgeCharacter>(TargetCharacter)) Character->SetNativeUse(false,0);return;
    }
    if(!IsNativeReady() || !UEControl || GetNativeHealth()<=0) return;
    auto* Character=Cast<ABridgeCharacter>(TargetCharacter);if(!Character) return;
    auto* PC=Controller(this);auto* Inventory=PC ? PC->GetNativeInventory() : nullptr;
    FVector Eye;FRotator Aim;Character->GetEyeAim(Eye,Aim);
    if(Action==TEXT("use_start") && LatestInput.HeldItem==TEXT("minecraft:bow")) {
        NativeBowStart=GetWorld()->GetTimeSeconds();Character->SetNativeUse(true,0);return;
    }
    if(Action==TEXT("use_release")) {
        const float Time=NativeBowStart<0 ? 0 : FMath::Clamp(float(GetWorld()->GetTimeSeconds()-NativeBowStart),0.f,1.f);
        NativeBowStart=-1;Character->SetNativeUse(false,0);
        const float Pull=FMath::Min(1.f,(Time*Time+2*Time)/3);
        if(Pull<.1f || LatestInput.HeldItem!=TEXT("minecraft:bow") || !SpawnBowProjectiles || Arrows.Num()>=64) return;
        if(auto* Arrow=GetWorld()->SpawnActor<ABridgeArrow>(Eye+Aim.Vector()*50,Aim)) {
            if(!NativeCreative && (!Inventory || !Inventory->ConsumeItem(TEXT("minecraft:arrow"),1))) {Arrow->Destroy();LastAction=TEXT("No arrows");return;}
            Arrow->Launch(Aim.Vector(),Pull,Character);Arrows.Add(Arrow);PlayNativeSound(TEXT("minecraft:entity.arrow.shoot"),Eye,1,1,TEXT("player"));
            OnBowFired(Eye,Aim.Vector(),Pull);
        }
        return;
    }
    FIntVector Voxel;FVector Normal,Hit;const bool Found=SyncedWorld->Aim(Eye,Aim,500,Voxel,Normal,Character,&Hit);
    if(Action==TEXT("pick")) {
        FString Id;FColor Tint;
        if(Found && Inventory && SyncedWorld->GetBlockInfo(Voxel,Id,Tint)) {
            if(NativeCreative) Inventory->AssignHotbar(Id,Inventory->GetSelectedSlot(),Inventory->MaxCount(Id));
            else {const auto& Slots=Inventory->GetSlots();for(int32 I=0;I<9;++I) if(Slots[I].ItemId==Id) {Inventory->SelectHotbar(I);break;}}
            NativeSelect(Inventory->GetSelectedItemId());
        }
        return;
    }
    if(Found && Action==TEXT("place") && (LatestInput.HeldItem==TEXT("minecraft:flint_and_steel") || LatestInput.HeldItem==TEXT("minecraft:fire_charge"))) {
        FString Id;FColor Tint;
        if(SyncedWorld->GetBlockInfo(Voxel,Id,Tint) && Id==TEXT("minecraft:tnt") && NativeFuses.Num()<64) {
            const FVector Position=SyncedWorld->BlockCenter(Voxel);
            if(SyncedWorld->BreakBlock(Voxel)) {NativeFuses.Add({Position,GetWorld()->GetTimeSeconds()+4});Character->SwingHand();PlayNativeSound(TEXT("minecraft:entity.tnt.primed"),Position,1,1,TEXT("block"));}
            return;
        }
    }
    if(Action==TEXT("place") && !NativeCreative && (!Inventory || Inventory->Selected().IsEmpty())) return;
    FBridgePacket Packet=LatestInput;Packet.Kind=EBridgeKind::BlockAction;Packet.Action=Action;
    Packet.Sequence=++LastSequence;Packet.EventId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Packet.ImportId=SyncedWorld->GetImportId();Packet.Yaw=Aim.Yaw;Packet.Pitch=-Aim.Pitch;
    BlockAction(Packet);
    if(!NativeCreative && Inventory && (LastAction==TEXT("placed") || LastAction.StartsWith(TEXT("mob spawned: ")))) {
        Inventory->ConsumeSelected(1);NativeSelect(Inventory->GetSelectedItemId());
    }
}
bool ABridgeReceiver::SpawnNativeDrop(const FString& ItemId,int32 Count,const FVector& Position,const FVector& Velocity) {
    const auto* Item=NativeUiPalette ? NativeUiPalette->FindItem(ItemId) : nullptr;
    if(!Item || Item->ModelKey.IsEmpty() || Count<1 || Count>Item->MaxCount || !EnsureItemWorld()) return false;
    const FString Tx=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    NativeDropItems.Add(Tx,ItemId);
    const FString Result=ItemWorld->Drop(Tx,ItemId,Item->ModelKey,Count,Item->MaxCount,Position,Velocity);
    ItemWorld->Resolve(Tx,0,0);
    if(Result!=TEXT("item_spawned")) {NativeDropItems.Remove(Tx);LastAction=Result;return false;}
    return true;
}
bool ABridgeReceiver::NativeDrop(const FString& ItemId,int32 Count) {
    if(!IsNativeReady() || !UEControl || !TargetCharacter) return false;
    auto* Character=Cast<ABridgeCharacter>(TargetCharacter);if(!Character) return false;
    FVector Eye;FRotator Aim;Character->GetEyeAim(Eye,Aim);
    return SpawnNativeDrop(ItemId,Count,Eye+Aim.Vector()*35,Aim.Vector()*300+FVector(0,0,100));
}
void ABridgeReceiver::NativeSetLighting(bool Enabled) {
    if(!NativePlayActive) return;NativeLighting=Enabled;
    Video->SetNativeRenderMode(Enabled);
    FString Failure;if(!FBridgeLightingService::SetEnvironment(GetWorld(),LatestInput.VanillaLight,!Enabled,&Failure))
        UE_LOG(LogTemp,Error,TEXT("Bridge native lighting not ready: %s; rerun native import"),*Failure);
    LogDiagnostics(FPlatformTime::Seconds(),true);
}
bool ABridgeReceiver::NativeSave() {
    if(!NativeInitialized || NativeRestoreFailed || !NativeStore.IsValid() || !NativeStore->IsReady() || NativeStore->IsSaving() || !IsValid(TargetCharacter)) return false;
    auto* Character=Cast<ABridgeCharacter>(TargetCharacter);auto* PC=Controller(this);if(!Character || !PC || !PC->IsSavedInventoryValid()) return false;
    auto* Contents=PC->GetNativeInventory();
    const auto Inventory=Contents ? Contents->ExportRuntimeState() : TSharedPtr<FJsonObject>();
    if(!Inventory.IsValid()) {
        NativeStatus=TEXT("Inventory snapshot unavailable; previous save preserved");
        UE_LOG(LogTemp,Error,TEXT("Bridge native save refused: %s"),*NativeStatus);return false;
    }
    auto Runtime=MakeShared<FJsonObject>();Runtime->SetNumberField(TEXT("health"),GetNativeHealth());Runtime->SetBoolField(TEXT("lighting"),NativeLighting);
    Runtime->SetArrayField(TEXT("respawn"),JsonVector(NativeRespawnPosition));Runtime->SetBoolField(TEXT("flying"),LatestInput.Flying);Runtime->SetNumberField(TEXT("perspective"),LatestInput.Perspective);
    Runtime->SetObjectField(TEXT("inventory"),Inventory);
    if(MobWorld) Runtime->SetArrayField(TEXT("mobs"),MobWorld->ExportNativeSnapshots(Anchor,SourceOrigin));
    if(ItemWorld) Runtime->SetArrayField(TEXT("drops"),ItemWorld->ExportNativeDrops(Anchor,SourceOrigin));
    TArray<TSharedPtr<FJsonValue>> Fuses;
    for(const auto& Fuse:NativeFuses) {
        auto Object=MakeShared<FJsonObject>();Object->SetArrayField(TEXT("position"),JsonVector(SourceOrigin+MinecraftDelta(Fuse.Position-Anchor)));
        Object->SetNumberField(TEXT("remainingSeconds"),FMath::Clamp(Fuse.Deadline-GetWorld()->GetTimeSeconds(),0.,4.));Fuses.Add(MakeShared<FJsonValueObject>(Object));
    }
    Runtime->SetArrayField(TEXT("fuses"),Fuses);
    const bool Started=NativeStore->BeginSave(SyncedWorld,SourceOrigin+MinecraftDelta(Character->GetMinecraftFeetPosition()-Anchor),PC->GetControlRotation(),Runtime);
    if(Started) {
        // Freeze entity physics as well as edits so the terrain and inventory,
        // drops, mobs and TNT all describe the same game-time snapshot.
        NativeSavePausedWorld=true;UGameplayStatics::SetGamePaused(this,true);
        UEControl=false;Character->SetAuthorityEnabled(false);
        if(MobWorld) MobWorld->SetAuthority(false,Character);
        if(ItemWorld) ItemWorld->SetAuthority(false,Character);
    }
    return Started;
}
void ABridgeReceiver::NativeRespawn() {
    if(!NativePlayActive || !NativeInitialized || NativeRestoreFailed || !NativeStore.IsValid() || NativeStore->IsSaving() || !TargetCharacter) return;
    if(MobWorld) MobWorld->RespawnPlayer();
    LatestInput.Flying=false;if(auto* PC=Controller(this)) PC->RestoreNativeView(LatestInput.Yaw,-LatestInput.Pitch,false,LatestInput.Perspective);
    const FVector Feet=BridgeProtocol::ToUnreal(NativeRespawnPosition-SourceOrigin,Anchor);
    SyncedWorld->EnsureCollisionForPosition(Feet);TargetCharacter->GetCharacterMovement()->StopMovementImmediately();
    TargetCharacter->SetActorLocation(Feet+FVector(0,0,TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),false,nullptr,ETeleportType::TeleportPhysics);
}
void ABridgeReceiver::PrepareNativeExit(UWorld* World) {
    if(World!=GetWorld() || NativeExitPrepared || !NativeStore.IsValid()) return;
    NativeExitPrepared=true;
    if(NativeInitialized && !NativeRestoreFailed) {
        if(NativeStore->IsSaving() && !NativeStore->FlushSave(60000)) {
            UE_LOG(LogTemp,Error,TEXT("Bridge native exit: pending save failed; previous save preserved: %s"),*NativeStore->GetError());return;
        }
        const bool Started=NativeSave();
        if(!Started || !NativeStore->FlushSave(60000)) UE_LOG(LogTemp,Error,TEXT("Bridge native exit: save did not complete; previous save preserved: %s"),*NativeStore->GetError());
    }
}
void ABridgeReceiver::PlayNativeSound(const FString& Id,const FVector& Position,float Volume,float Pitch,const FString& RequestedCategory) {
    if(Id.IsEmpty() || NativeMasterVolume<=0) return;
    const auto* Event=NativeSoundPalette ? NativeSoundPalette->Find(Id) : nullptr;
    if(!Event || Event->Variants.IsEmpty()) {
        if(!MissingNativeSounds.Contains(Id)) {MissingNativeSounds.Add(Id);UE_LOG(LogTemp,Warning,TEXT("Bridge native audio missing: %s palette=%s"),*Id,*GetNameSafe(NativeSoundPalette));}
        return;
    }
    int32 Total=0;for(const auto& Variant:Event->Variants) if(Variant.Wave) Total+=FMath::Clamp(Variant.Weight,1,1000);
    if(Total<=0) return;
    int32 Choice=FMath::RandRange(0,Total-1);
    const FBridgeNativeSoundVariant* Selected=nullptr;
    for(const auto& Variant:Event->Variants) if(Variant.Wave) {Choice-=FMath::Clamp(Variant.Weight,1,1000);if(Choice<0) {Selected=&Variant;break;}}
    if(!Selected) return;
    FString Category=RequestedCategory.IsEmpty() ? (Id.Contains(TEXT("block.")) ? TEXT("block") : Id.Contains(TEXT("entity.player.")) ? TEXT("player") : TEXT("neutral")) : RequestedCategory;
    const float CategoryVolume=Category!=TEXT("master") && NativeSoundVolumes.Contains(Category) ? NativeSoundVolumes[Category] : 1.f;
    if(Id.StartsWith(TEXT("minecraft:ui."))) {
        UGameplayStatics::PlaySound2D(this,Selected->Wave,FMath::Clamp(Volume*Selected->Volume*NativeMasterVolume*CategoryVolume,0.f,4.f),FMath::Clamp(Pitch*Selected->Pitch,.1f,4.f));return;
    }
    UGameplayStatics::PlaySoundAtLocation(this,Selected->Wave,Position,FRotator::ZeroRotator,
        FMath::Clamp(Volume*Selected->Volume*NativeMasterVolume*CategoryVolume,0.f,4.f),FMath::Clamp(Pitch*Selected->Pitch,.1f,4.f),0,NativeSoundAttenuation);
}
void ABridgeReceiver::LogDiagnostics(double Now,bool bForceLog) {
    if(!bForceLog && NativeLastDiagnostic>=0 && Now-NativeLastDiagnostic<5) return;
    NativeLastDiagnostic=Now;
    if(NativePlayActive && ItemWorld) {
        const auto Live=ItemWorld->GetTransactionIds();
        for(auto It=NativeDropItems.CreateIterator();It;++It) if(!Live.Contains(It.Key())) It.RemoveCurrent();
        for(auto It=NativeDropRevisions.CreateIterator();It;++It) if(!Live.Contains(It.Key())) It.RemoveCurrent();
    }
    FString Sample=TEXT("lighting field unavailable");
    if(SyncedWorld && SyncedWorld->GetLighting() && TargetCharacter) {
        const FVector Source=SourceOrigin+MinecraftDelta(TargetCharacter->GetActorLocation()-Anchor);
        Sample=SyncedWorld->GetLighting()->Describe(FIntVector(FMath::FloorToInt(Source.X),FMath::FloorToInt(Source.Y),FMath::FloorToInt(Source.Z)));
    }
    const double Fps=PerformanceSeconds>0 ? PerformanceFrames/PerformanceSeconds : 0;
    UE_LOG(LogTemp,Display,TEXT("Bridge diagnostics: mode=%s fps=%.1f frameMs=%.2f cells=%d faces=%d sections=%d pending=%d lightPending=%d mobPalette=%s appearances=%d templates=%d mobs=%d dust=%d input=%s status=%s action=%s %s %s"),
        NativePlayActive?TEXT("native"):TEXT("bridge"),Fps,PerformanceFrames>0?PerformanceSeconds*1000/PerformanceFrames:0,
        SyncedWorld?SyncedWorld->CellCount():0,SyncedWorld?SyncedWorld->RenderedFaceCount():0,SyncedWorld?SyncedWorld->RenderSectionCount():0,
        SyncedWorld?SyncedWorld->RebuildPending():0,SyncedWorld && SyncedWorld->GetLighting()?SyncedWorld->GetLighting()->Pending():0,
        *GetNameSafe(MobPalette),MobPalette?MobPalette->Appearances.Num():0,MobPalette?MobPalette->Templates.Num():0,MobWorld?MobWorld->AliveCount():0,
        VanillaEffects?VanillaEffects->ParticleCount():0,UEControl?TEXT("ready"):TEXT("waiting"),*NativeStatus,*LastAction,*Sample,*Video->GetDiagnosticSummary());
    if(NativePlayActive) {PerformanceSeconds=0;PerformanceFrames=0;}
}
