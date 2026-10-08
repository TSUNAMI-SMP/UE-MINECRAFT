#include "BridgeReceiver.h"
#include "BridgeBlockPreview.h"
#include "BridgeArrow.h"
#include "BridgeCharacter.h"
#include "BridgeWorld.h"
#include "BridgeVideo.h"
#include "BridgeBlockPalette.h"
#include "BridgePlayerAppearance.h"
#include "BridgeVanillaEffects.h"
#include "BridgeMobWorld.h"
#include "BridgeItemWorld.h"
#include "BridgeDroppedItem.h"
#include "BridgeLightingService.h"
#include "BridgeNativeWorldStore.h"
#include "BridgeNativeInventory.h"
#include "BridgeNativePlayerController.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Field/FieldSystemObjects.h"
#include "EngineUtils.h"
#include "Engine/World.h"

ABridgeReceiver::ABridgeReceiver() {
    PrimaryActorTick.bCanEverTick = true;
    Video=CreateDefaultSubobject<UBridgeVideo>(TEXT("Video"));
}
void ABridgeReceiver::AcquireTarget() {
    if (!IsValid(TargetCharacter)) { TargetCharacter = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0)); Anchored = false; }
    if (TargetCharacter && !Anchored) {
        Anchor = NativePlayActive ? FVector::ZeroVector : TargetCharacter->GetActorLocation();
        if(!NativePlayActive) Anchor.Z -= TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TargetCharacter->GetCharacterMovement()->DisableMovement();
        TargetCharacter->GetCharacterMovement()->AddTickPrerequisiteActor(this);
        Video->AddTickPrerequisiteActor(TargetCharacter);
        Video->AddTickPrerequisiteComponent(TargetCharacter->GetCharacterMovement());
        Anchored = true;
    }
}
void ABridgeReceiver::BeginPlay() {
    Super::BeginPlay(); AcquireTarget();
    InstanceId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    if(PreferNativePlay && !NativeWorldFile.IsEmpty()) { BeginNativePlay(); return; }
    ISocketSubsystem* S = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (Port < 1024 || Port > 65535 || !S) { UE_LOG(LogTemp, Error, TEXT("Bridge: invalid port/socket subsystem")); return; }
    Socket = S->CreateSocket(NAME_DGram, TEXT("MinecraftBridge"), false);
    bool Valid = false; auto Address = S->CreateInternetAddr();
    Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(Port);
    if (!Socket || !Valid || !Socket->SetNonBlocking(true) || !Socket->Bind(*Address)) {
        UE_LOG(LogTemp, Error, TEXT("Bridge: cannot bind 127.0.0.1:%d"), Port);
        if (Socket) { Socket->Close(); S->DestroySocket(Socket); Socket = nullptr; }
        return;
    }
    int32 ActualBuffer; Socket->SetReceiveBufferSize(256 * 1024, ActualBuffer);
    VanillaEffects=GetWorld()->SpawnActor<ABridgeVanillaEffects>();
    if(IsValid(VanillaEffects)) {
        Video->AddTickPrerequisiteActor(VanillaEffects);
        VanillaEffects->SampleLight=[this](const FVector& Position){
            if(!IsValid(SyncedWorld) || !SyncedWorld->GetLighting()) return FLinearColor(1,0,1,1);
            const FVector Relative=(Position-Anchor)/100.0;
            const FVector Source=SourceOrigin+FVector(-Relative.Y,Relative.Z,Relative.X);
            return SyncedWorld->GetLighting()->Sample(FIntVector(FMath::FloorToInt(Source.X),FMath::FloorToInt(Source.Y),FMath::FloorToInt(Source.Z)));
        };
    }
    UE_LOG(LogTemp, Display, TEXT("Bridge 0.14.0 listening on 127.0.0.1:%d"), Port);
    Video->Start(VideoPort);
    if (!TargetCharacter) UE_LOG(LogTemp, Warning, TEXT("Bridge: waiting for player Character; will retry every tick"));
    if (!ExplosionSystem) UE_LOG(LogTemp, Warning, TEXT("Bridge: ExplosionSystem is unset; Niagara will not play"));
}
void ABridgeReceiver::EndPlay(const EEndPlayReason::Type Reason) {
    if(NativePlayActive && NativeStore.IsValid()) {
        if(!NativeExitPrepared) PrepareNativeExit(GetWorld());
        FWorldDelegates::OnWorldBeginTearDown.Remove(NativeTearDownHandle);
        NativeStore.Reset();
        Video->RestoreNativeRenderMode();
    }
    if (IsValid(Preview)) Preview->Destroy();
    if (IsValid(SyncedWorld)) SyncedWorld->Destroy();
    if (IsValid(VanillaEffects)) VanillaEffects->Destroy();
    if(IsValid(MobWorld)) MobWorld->Destroy();
    if(IsValid(ItemWorld)) ItemWorld->Destroy();
    PendingFeedback.Empty();
    for (auto& Arrow : Arrows) if (IsValid(Arrow)) Arrow->Destroy();
    if (Socket) { Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); Socket = nullptr; }
    Super::EndPlay(Reason);
}
void ABridgeReceiver::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds); AcquireTarget();
    Arrows.RemoveAll([](const auto& Arrow) { return !IsValid(Arrow); });
    if(NativePlayActive) { TickNativePlay(DeltaSeconds); return; }
    if (!Socket) return;
    uint8 Bytes[BridgeProtocol::MaxPacketBytes + 1];
    for (int32 I = 0; I < 256; ++I) {
        uint32 Pending = 0; if (!Socket->HasPendingData(Pending)) break;
        int32 Count = 0; auto Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        if (!Socket->RecvFrom(Bytes, sizeof(Bytes), Count, *Sender)) break;
        uint32 SenderIp = 0; Sender->GetIp(SenderIp);
        if (Count <= 0 || Count > BridgeProtocol::MaxPacketBytes || SenderIp != 0x7f000001) { ++InvalidPackets; continue; }
        FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes), Count);
        FString Json(Text.Length(), Text.Get()); TSharedPtr<FJsonObject> Object; FBridgePacket Packet;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !BridgeProtocol::Parse(Object, Packet)) {
            ++InvalidPackets; continue;
        }
        Process(Packet, Sender);
    }
    // Drain first, apply only the newest pose once per UE frame; no backlog of teleports.
    const bool Sealed=IsValid(SyncedWorld) && SyncedWorld->IsSealed();
    if (HasNewInput && TargetCharacter && !Sealed) {
        if (auto* Bridge=Cast<ABridgeCharacter>(TargetCharacter)) Bridge->ApplyMinecraftPose(LatestInput.BodyHeight,LatestInput.EyeHeight,IsValid(SyncedWorld) && SyncedWorld->IsImporting() ? false : LatestInput.Sneak);
        else {
            const float HalfHeight=float(LatestInput.BodyHeight*50);
            TargetCharacter->GetCapsuleComponent()->SetCapsuleSize(FMath::Min(30.f,HalfHeight),HalfHeight,false);
            if (auto* Camera=TargetCharacter->FindComponentByClass<UCameraComponent>()) Camera->SetRelativeLocation(FVector(0,0,LatestInput.EyeHeight*100-HalfHeight));
        }
        const FVector Capsule(0, 0, TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        TargetCharacter->SetActorLocation(BridgeProtocol::ToUnreal(LatestInput.Position, Anchor) + Capsule, false, nullptr, ETeleportType::TeleportPhysics);
        TargetCharacter->SetActorRotation(BridgeProtocol::ToRotation(LatestInput.Yaw, 0));
        if (AController* C = TargetCharacter->GetController()) C->SetControlRotation(BridgeProtocol::ToRotation(LatestInput.Yaw, LatestInput.Pitch));
    }
    HasNewInput = false;
    const double Now = FPlatformTime::Seconds(); Connected = !Session.IsEmpty() && LastSequence > 0 && Now - LastInput <= 0.25;
    if (!Connected) { ForwardInput = RightInput = 0; JumpHeld = false; }
    if(auto* Bridge=Cast<ABridgeCharacter>(TargetCharacter)) {
        UEControl=Connected && Sealed && LatestInput.Controller;
        const bool Alive=!IsValid(MobWorld) || MobWorld->PlayerHealth>0;
        TerrainMovementReady=!Sealed || SyncedWorld->IsMovementReady(Bridge->GetMinecraftFeetPosition(),Bridge->GetVelocity());
        Bridge->SetAuthorityEnabled(UEControl && Alive && TerrainMovementReady);
        Bridge->ConfigureVisuals(PreviewMaterial,TexturePalette,LatestInput.HeldItem,LatestInput.HeldBlock,LatestInput.HeldColor,LatestInput.HeldModelKey);
        Bridge->ConfigureAppearance(PlayerAppearance);
        Bridge->ConfigureOutline(OutlineMaterial);
        Bridge->SetMinecraftFov(float(LatestInput.CameraFov));
        Bridge->ApplyPlayerVisuals(LatestInput.Perspective,float(LatestInput.SwingProgress),float(LatestInput.EquipProgress),LatestInput.UsingItem,
            LatestInput.UseAction,float(LatestInput.UseProgress),LatestInput.LeftHanded,LatestInput.SkinLayers,LatestInput.SlimArms);
        Bridge->SetInteractionWorld(SyncedWorld);
        if(UEControl && Alive && TerrainMovementReady) {
            if(auto* C=Bridge->GetController()) C->SetControlRotation(BridgeProtocol::ToRotation(LatestInput.Yaw,LatestInput.Pitch));
            Bridge->ApplyFlight(LatestInput.Creative,LatestInput.Flying);
            Bridge->ApplyUEInput(ForwardInput,RightInput,JumpHeld,SneakHeld,LatestInput.Sprint);
        }
        if(IsValid(VanillaEffects)) {
            VanillaEffects->Configure(SyncedWorld,TexturePalette,VanillaParticleMaterial);
            VanillaEffects->SetViewCamera(Bridge->BridgeCamera);
            VanillaEffects->AddTickPrerequisiteActor(Bridge);
            VanillaEffects->AddTickPrerequisiteComponent(Bridge->GetCharacterMovement());
            if(UEControl) {
                TArray<FBridgeVanillaEvent> Events;VanillaEffects->SampleCharacter(Bridge,DeltaSeconds,Events);
                for(const auto& Effect:Events) QueueFeedback(Effect.Type,Effect.BlockId,Effect.Position,Effect.FallDistance);
            } else VanillaEffects->ResetMovement();
        }
    } else UEControl=false;
    if(IsValid(MobWorld)) {MobWorld->Palette=MobPalette;MobWorld->SetCreative(LatestInput.Creative);MobWorld->SetAuthority(UEControl,TargetCharacter);}
    if(IsValid(SyncedWorld)) SyncedWorld->InteractionSound=[this](const FString& Type,const FString& Block,const FVector& Position){QueueFeedback(Type,Block,Position);};
    if(IsValid(ItemWorld)) {
        if(!LatestInput.ItemSession) ItemWorld->Clear();
        else ItemWorld->SetAuthority(UEControl && (!IsValid(MobWorld) || MobWorld->PlayerHealth>0),TargetCharacter);
    }
    if(IsValid(SyncedWorld) && TargetCharacter) {
        FVector Feet=TargetCharacter->GetActorLocation();Feet.Z-=TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TArray<FVector> Extra;
        if(IsValid(MobWorld)) Extra.Append(MobWorld->CollisionAnchors());
        if(IsValid(ItemWorld)) Extra.Append(ItemWorld->CollisionAnchors());
        SyncedWorld->UpdateCollisionCenters(Feet,Extra);
    }
    if(LastLightActors<0 || Now-LastLightActors>=.1) {
        LastLightActors=Now;
        FBridgeLightingService::SetEnvironment(GetWorld(),LatestInput.VanillaLight,!Video->IsLightingEnabled());
        for(TActorIterator<AActor> It(GetWorld());It;++It) if(It->ActorHasTag(TEXT("BridgeMinecraftVisual"))) LightActor(*It);
        LightActor(TargetCharacter);
    }
    PerformanceSeconds+=DeltaSeconds;++PerformanceFrames;
    LogDiagnostics(Now);
    if(Connected && Peer.IsValid() && (LastPerformance<0 || Now-LastPerformance>=1)) {LastPerformance=Now;SendPerformance();}
    PumpFeedback(Now);
    if(Sealed && Peer.IsValid() && (LastPose<0 || Now-LastPose>=1.0/60)) { LastPose=Now; SendPose(); }
    Video->SetSource(TargetCharacter ? TargetCharacter->FindComponentByClass<UCameraComponent>() : nullptr, Connected ? Session : FString(),LastSequence);
    for (auto It = SeenEvents.CreateIterator(); It; ++It) if (Now - It.Value() > 10) { EventResults.Remove(It.Key()); It.RemoveCurrent(); }
    if (!StagedBatches.IsEmpty() && Now > StagingDeadline) {
        StagedBatches.Empty(); StagingId.Empty(); ExpectedBatches = 0;
        UE_LOG(LogTemp, Warning, TEXT("Bridge: incomplete preview snapshot expired; previous view retained"));
    }
}
void ABridgeReceiver::Process(const FBridgePacket& P, const TSharedRef<FInternetAddr>& Sender) {
    const double Now = FPlatformTime::Seconds(); uint32 SenderIp = 0; Sender->GetIp(SenderIp);
    if(P.Kind==EBridgeKind::FeedbackAck) {
        // ACKs cannot acquire or renew an input lease and cannot mutate terrain.
        if(Session==P.Session && PeerPort==Sender->GetPort() && PeerAddress==SenderIp) PendingFeedback.Remove(P.EventId);
        return;
    }
    if(P.Kind==EBridgeKind::ItemDrop || P.Kind==EBridgeKind::ItemResolve) {
        // Auxiliary transactions cannot acquire or renew the input lease.
        if(Session!=P.Session || PeerPort!=Sender->GetPort() || PeerAddress!=SenderIp || !Peer.IsValid()) return;
        if(P.Kind==EBridgeKind::ItemResolve) {if(IsValid(ItemWorld)) ItemWorld->Resolve(P.ItemTx,P.ItemRevision,P.ItemAccepted);return;}
        FString Reason;
        if(!LatestInput.ItemSession || P.ItemEpoch.IsEmpty() || P.ItemEpoch!=LatestInput.ItemEpoch) Reason=TEXT("item_session_expired");
        else if(!Connected || Now-LastInput>.25 || !UEControl || !TargetCharacter) Reason=TEXT("item_controller_not_ready");
        else if(!IsValid(SyncedWorld) || !SyncedWorld->IsSealed() || P.ImportId!=SyncedWorld->GetImportId()) Reason=TEXT("item_import_changed");
        const FVector Position=BridgeProtocol::ToUnreal(P.ItemPosition,Anchor);
        if(Reason.IsEmpty() && (FVector::DistSquared(Position,TargetCharacter->GetActorLocation())>FMath::Square(300.f)
            || P.ItemVelocity.SizeSquared()>FMath::Square(30.0))) Reason=TEXT("item_drop_out_of_reach");
        if(!Reason.IsEmpty()) {QueueItemResult(P.ItemTx,0,TEXT("rejected"),P.ItemCount,Reason);return;}
        if(!EnsureItemWorld()) {QueueItemResult(P.ItemTx,0,TEXT("rejected"),P.ItemCount,TEXT("item_world_unavailable"));return;}
        SyncedWorld->EnsureCollisionForPosition(Position);
        ItemWorld->Drop(P.ItemTx,P.ItemId,P.ItemModelKey,P.ItemCount,P.ItemMaxCount,Position,BridgeProtocol::ToDirection(P.ItemVelocity)*100.0);
        return;
    }
    if (Session != P.Session || PeerPort != Sender->GetPort() || PeerAddress != SenderIp) {
        if (!Session.IsEmpty() && Now - LastPacket < 0.5) return;
        Session = P.Session; PeerPort = Sender->GetPort(); PeerAddress = SenderIp;
        LastSequence = 0; LastInput = 0; LastStatus = -1;LastPerformance=-1;PerformanceFrames=0;PerformanceSeconds=0; SeenEvents.Empty();EventResults.Empty();PendingFeedback.Empty();HasNewInput = false;
        if(IsValid(ItemWorld)) ItemWorld->Clear();
        if(IsValid(VanillaEffects)) VanillaEffects->ResetMovement();
        JumpHeld = SneakHeld = false; ForwardInput = RightInput = 0; ClearPreview(0);
        if (IsValid(SyncedWorld)) { if(!SyncedWorld->IsSealed()) SyncedWorld->Clear(); else SyncedWorld->NewSource(); }
        LatestInput=FBridgePacket(); LastActionSequence=0;LastActionAt=-1; PoseSequence=0; LastPose=-1; Peer=Sender;
    }
    if (P.Kind == EBridgeKind::Input) {
        if (P.Sequence <= LastSequence) return;
        Peer=Sender;
        LastSequence = P.Sequence; LastInput = LastPacket = Now;
        ForwardInput = P.Forward; RightInput = P.Right;
        if (P.Jump && !JumpHeld) OnJumpPressed(); JumpHeld = P.Jump;
        SneakHeld=P.Sneak;
        if(IsValid(ItemWorld) && LatestInput.ItemEpoch!=P.ItemEpoch) ItemWorld->Clear();
        LatestInput = P; HasNewInput = true;
        if (LastStatus < 0 || Now - LastStatus >= 0.25) { LastStatus = Now; SendStatus(Sender); }
        return;
    }
    if (!Anchored) return; // Retry the event after the player/spawn anchor becomes available.
    bool Accepted=true;FString ResultReason;
    if (!SeenEvents.Contains(P.EventId)) {
        if (SeenEvents.Num() >= 32768) return;
        const FVector Position = BridgeProtocol::ToUnreal(P.Position, Anchor);
        switch (P.Kind) {
            case EBridgeKind::BlockAction: BlockAction(P);break;
            case EBridgeKind::MobSpawn: {
                if(!IsValid(SyncedWorld) || !SyncedWorld->IsSealed() || SyncedWorld->GetImportId()!=P.ImportId || !UEControl) return;
                if(!EnsureMobWorld()) {Accepted=false;ResultReason=TEXT("mob_world_unavailable");break;}
                Accepted=MobWorld->Import(P.Mob,Anchor);ResultReason=MobWorld->LastReason;
                break;
            }
            case EBridgeKind::MobTemplateSpawn: {
                if(!IsValid(SyncedWorld) || !SyncedWorld->IsSealed() || SyncedWorld->GetImportId()!=P.ImportId || !UEControl) return;
                if(!EnsureMobWorld()) {Accepted=false;ResultReason=TEXT("mob_world_unavailable");break;}
                LastAction=MobWorld->SpawnEgg(P.SpawnType,Position,Anchor,P.EventId);ResultReason=LastAction;
                Accepted=LastAction.StartsWith(TEXT("mob spawned:")) || LastAction.StartsWith(TEXT("mob already spawned:"));break;
            }
            case EBridgeKind::MobClear:
                if(!IsValid(SyncedWorld) || SyncedWorld->GetImportId()!=P.ImportId) return;
                if(IsValid(MobWorld)) MobWorld->Clear();
                break;
            case EBridgeKind::PlayerRespawn: {
                auto* Character=Cast<ABridgeCharacter>(TargetCharacter);
                if(!UEControl || !Character || !IsValid(MobWorld) || MobWorld->PlayerHealth>0 || !IsValid(SyncedWorld)
                    || SyncedWorld->GetImportId()!=P.ImportId) return;
                FCollisionQueryParams Query(SCENE_QUERY_STAT(BridgeRespawn),false,Character);
                const float Half=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                const float Radius=Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
                bool Restored=false;
                for(int32 Step=0;Step<4;++Step) {
                    const FVector Spawn=Anchor+FVector(0,0,Half+3+Step*100);
                    if(!GetWorld()->OverlapBlockingTestByChannel(Spawn,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)) {
                        Character->SetActorLocation(Spawn,false,nullptr,ETeleportType::TeleportPhysics);
                        Character->GetCharacterMovement()->StopMovementImmediately();
                        MobWorld->RespawnPlayer();Restored=true;break;
                    }
                }
                LastAction=Restored ? TEXT("respawned") : TEXT("respawn point blocked; remove obstruction in UE");
                break;
            }
            case EBridgeKind::Tnt: if(!UEControl) Explode(Position); break;
            case EBridgeKind::Bow: {
                if(UEControl) break;
                const FVector Direction = BridgeProtocol::ToDirection(P.Direction);
                if (SpawnBowProjectiles && Arrows.Num() < 64) {
                    FActorSpawnParameters Params; Params.Owner = TargetCharacter; Params.Instigator = TargetCharacter;
                    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    if (auto* Arrow = GetWorld()->SpawnActor<ABridgeArrow>(Position, Direction.Rotation(), Params)) {
                        Arrow->Launch(Direction, P.Pull, TargetCharacter); Arrows.Add(Arrow);
                    }
                }
                OnBowFired(Position, Direction, P.Pull); break;
            }
            case EBridgeKind::Snapshot: if (!HandleSnapshot(P)) return; break;
            case EBridgeKind::ClearPreview: if (P.Sequence >= PreviewGeneration) ClearPreview(P.Sequence); break;
            case EBridgeKind::VideoConfig:
                Video->Width=P.VideoWidth; Video->Height=P.VideoHeight; Video->FramesPerSecond=P.VideoFps;
                Video->Quality=P.VideoQuality; Video->ExposureCompensation=float(P.VideoExposure);
                Video->SetRenderMode(P.Lighting,P.VanillaSky);
                FBridgeLightingService::SetEnvironment(GetWorld(),LatestInput.VanillaLight,!P.Lighting);
                if(IsValid(VanillaEffects)) VanillaEffects->ConfigureParticleTuning(float(P.ParticleScale),float(P.ParticleDensity),float(P.ParticleLifetime));
                break;
            case EBridgeKind::WorldBegin: {

                if(!Cast<ABridgeCharacter>(TargetCharacter)) return;
                if(!IsValid(SyncedWorld)) SyncedWorld=GetWorld()->SpawnActor<ABridgeWorld>();
                if(!SyncedWorld) return;
                SyncedWorld->AddTickPrerequisiteActor(this);
                TargetCharacter->GetCharacterMovement()->AddTickPrerequisiteActor(SyncedWorld);
                Video->AddTickPrerequisiteActor(SyncedWorld);
                const bool ReplaceImport=SyncedWorld->GetImportId()!=P.ImportId;
                if(!SyncedWorld->BeginImport(P,Anchor)) return;
                if(SyncedWorld->GetImportId()!=P.ImportId) break;
                Video->SetMinecraftOrigin(P.MinecraftOrigin,Anchor);SourceOrigin=P.MinecraftOrigin;
                if(ReplaceImport && SyncedWorld->GetImportId()==P.ImportId) {
                    if(IsValid(MobWorld)) MobWorld->Clear();
                    if(IsValid(ItemWorld)) ItemWorld->Clear();
                    if(auto* Bridge=Cast<ABridgeCharacter>(TargetCharacter)) { Bridge->SetAuthorityEnabled(false); Bridge->ApplyMinecraftPose(1.8,1.62,false); }
                    UEControl=false;
                    TargetCharacter->SetActorLocation(Anchor+FVector(0,0,90),false,nullptr,ETeleportType::TeleportPhysics);
                }
                break;
            }
            case EBridgeKind::WorldCommit:
                if(!IsValid(SyncedWorld) || !SyncedWorld->CommitImport(P)) return;
                break;
            case EBridgeKind::WorldCell:
            case EBridgeKind::WorldScope:
            case EBridgeKind::WorldClear:
                if (!IsValid(SyncedWorld)) SyncedWorld=GetWorld()->SpawnActor<ABridgeWorld>();
                if (!SyncedWorld || !SyncedWorld->Handle(P,Anchor,PreviewMaterial,TexturePalette)) return;
                break;
            default: return;
        }
        SeenEvents.Add(P.EventId, Now);
        EventResults.Add(P.EventId,FEventResult{Accepted,ResultReason.Left(160)});
    }
    LastPacket = Now;
    if(const auto* Result=EventResults.Find(P.EventId)) {Accepted=Result->Accepted;ResultReason=Result->Reason;}
    SendAck(P,Sender,Accepted,ResultReason);
}
void ABridgeReceiver::SendAck(const FBridgePacket& P,const TSharedRef<FInternetAddr>& Sender,bool Accepted,const FString& Reason) {
    auto Ack = MakeShared<FJsonObject>(); Ack->SetNumberField(TEXT("v"), 1); Ack->SetStringField(TEXT("kind"), TEXT("ack"));
    Ack->SetStringField(TEXT("session"), Session); Ack->SetStringField(TEXT("eventId"), P.EventId);
    Ack->SetStringField(TEXT("receiverId"),InstanceId);Ack->SetNumberField(TEXT("seq"),double(P.Sequence));
    Ack->SetBoolField(TEXT("accepted"),Accepted);Ack->SetStringField(TEXT("reason"),Reason.Left(160));SendJson(Ack, Sender);
}
void ABridgeReceiver::SendJson(const TSharedRef<FJsonObject>& Json, const TSharedRef<FInternetAddr>& Sender) {
    FString Text; FJsonSerializer::Serialize(Json, TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    FTCHARToUTF8 Utf8(*Text); int32 Sent = 0;
    if(Utf8.Length()>BridgeProtocol::MaxPacketBytes) {UE_LOG(LogTemp,Warning,TEXT("Bridge: outbound UDP packet exceeds budget (%d bytes)"),Utf8.Length());return;}
    Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, *Sender);
}
void ABridgeReceiver::SendStatus(const TSharedRef<FInternetAddr>& Sender) {
    int32 Walls = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It) if (It->ActorHasTag(TEXT("BridgeWall"))) {
        TInlineComponentArray<UGeometryCollectionComponent*> Components; It->GetComponents(Components);
        for (auto* GC : Components) if (GC && GC->GetRestCollection()) ++Walls;
    }
    const UCameraComponent* Camera = TargetCharacter ? TargetCharacter->FindComponentByClass<UCameraComponent>() : nullptr;
    auto Reply = MakeShared<FJsonObject>(); Reply->SetNumberField(TEXT("v"), 1); Reply->SetStringField(TEXT("kind"), TEXT("status"));
    Reply->SetStringField(TEXT("session"), Session); Reply->SetNumberField(TEXT("seq"), double(LastSequence));
    Reply->SetStringField(TEXT("receiver"), TEXT("ue"));
    Reply->SetBoolField(TEXT("cameraReady"), Camera && Camera->IsActive() && TargetCharacter->GetController());
    Reply->SetBoolField(TEXT("vfxReady"), IsValid(ExplosionSystem)); Reply->SetNumberField(TEXT("walls"), Walls);
    Reply->SetNumberField(TEXT("previewBlocks"), PreviewBlocks);
    Reply->SetStringField(TEXT("build"),TEXT("0.14.0"));
    Reply->SetBoolField(TEXT("blockModelsV2"),true); Reply->SetBoolField(TEXT("videoV3"),true);
    Reply->SetBoolField(TEXT("blockPaletteReady"),IsValid(TexturePalette) && !TexturePalette->BlockstateDefinitions.IsEmpty()
        && !TexturePalette->StateShapes.IsEmpty() && !TexturePalette->FaceMaterials.IsEmpty());
    Reply->SetBoolField(TEXT("terrainV2"),true);Reply->SetBoolField(TEXT("itemDropsV1"),true);Reply->SetBoolField(TEXT("videoGpuV1"),true);
    Reply->SetBoolField(TEXT("itemsV1"),true);Reply->SetBoolField(TEXT("creativeFlightV1"),true);
    Reply->SetNumberField(TEXT("itemModelCount"),IsValid(TexturePalette) ? TexturePalette->ItemModels.Num() : 0);
    if(const auto* Character=Cast<ABridgeCharacter>(TargetCharacter)) {
        Reply->SetStringField(TEXT("heldModel"),Character->HeldModelStatus.Left(64));
        Reply->SetBoolField(TEXT("flying"),Character->BridgeFlying);
    }
    Reply->SetStringField(TEXT("videoColor"),TEXT("single sRGB; straight alpha"));
    Reply->SetNumberField(TEXT("mobTemplateCount"),IsValid(MobPalette) ? MobPalette->Templates.Num() : 0);
    Reply->SetBoolField(TEXT("skySupported"),true);
    Reply->SetBoolField(TEXT("lightingEnabled"),Video->IsLightingEnabled());
    Reply->SetBoolField(TEXT("vanillaSkyEnabled"),Video->IsVanillaSkyEnabled());
    Reply->SetBoolField(TEXT("maskCountsAvailable"),Video->AreMaskCountsAvailable());
    Reply->SetNumberField(TEXT("maskPixels"),Video->GetMaskPixels());
    Reply->SetNumberField(TEXT("maskForeground"),Video->GetMaskForegroundPixels());
    Reply->SetNumberField(TEXT("maskTranslucent"),Video->GetMaskTranslucentPixels());
    Reply->SetBoolField(TEXT("mobsV1"),true);Reply->SetBoolField(TEXT("mobPaletteReady"),IsValid(MobPalette));
    Reply->SetNumberField(TEXT("mobCount"),IsValid(MobWorld) ? MobWorld->AliveCount() : 0);
    Reply->SetNumberField(TEXT("mobMissing"),IsValid(MobWorld) ? MobWorld->MissingAppearance : 0);
    Reply->SetNumberField(TEXT("uePlayerHealth"),IsValid(MobWorld) ? MobWorld->PlayerHealth : 20);
    Reply->SetStringField(TEXT("mobReason"),IsValid(MobWorld) ? MobWorld->LastReason.Left(80) : TEXT("not_imported"));
    Reply->SetStringField(TEXT("receiverId"),InstanceId);
    Reply->SetBoolField(TEXT("worldV1"),true); Reply->SetBoolField(TEXT("videoV1"),true);
    Reply->SetBoolField(TEXT("blockTexturesV1"),true); Reply->SetBoolField(TEXT("videoControlsV1"),true);
    Reply->SetBoolField(TEXT("blockActionsV1"),true);Reply->SetBoolField(TEXT("videoV2"),true);
    Reply->SetBoolField(TEXT("playerVisualsV1"),true);Reply->SetBoolField(TEXT("vanillaFeedbackV1"),true);
    Reply->SetBoolField(TEXT("skinReady"),IsValid(PlayerAppearance));
    const auto Dust=IsValid(VanillaEffects) ? VanillaEffects->GetDiagnostics() : FBridgeDustDiagnostics();
    Reply->SetBoolField(TEXT("particlesReady"),IsValid(VanillaEffects) && Dust.Reason==TEXT("ready"));
    auto ParticleStatus=MakeShared<FJsonObject>();
    ParticleStatus->SetStringField(TEXT("reason"),IsValid(VanillaEffects) ? Dust.Reason : TEXT("missing_effects"));
    ParticleStatus->SetBoolField(TEXT("materialReady"),Dust.MaterialReady);
    ParticleStatus->SetNumberField(TEXT("textureCount"),Dust.TextureCount);
    ParticleStatus->SetNumberField(TEXT("requested"),double(Dust.Requested));
    ParticleStatus->SetNumberField(TEXT("spawned"),double(Dust.Spawned));
    ParticleStatus->SetNumberField(TEXT("rejected"),double(Dust.Rejected));
    ParticleStatus->SetNumberField(TEXT("active"),Dust.Active);
    ParticleStatus->SetNumberField(TEXT("instances"),Dust.Instances);
    ParticleStatus->SetNumberField(TEXT("peakInstances"),Dust.PeakInstances);
    ParticleStatus->SetNumberField(TEXT("groups"),Dust.Groups);
    ParticleStatus->SetStringField(TEXT("lastType"),Dust.LastType);
    ParticleStatus->SetStringField(TEXT("lastBlock"),Dust.LastBlock.Left(64));
    ParticleStatus->SetNumberField(TEXT("lastRequested"),Dust.LastRequested);
    ParticleStatus->SetNumberField(TEXT("lastSpawned"),Dust.LastSpawned);
    ParticleStatus->SetStringField(TEXT("lastReason"),Dust.LastReason);
    Reply->SetObjectField(TEXT("particles"),ParticleStatus);
    Reply->SetStringField(TEXT("lastAction"),LastAction.Left(64));
    Reply->SetBoolField(TEXT("authorityV1"),Cast<ABridgeCharacter>(TargetCharacter)!=nullptr);
    Reply->SetBoolField(TEXT("ueControl"),UEControl);Reply->SetBoolField(TEXT("terrainMovementReady"),TerrainMovementReady);
    Reply->SetBoolField(TEXT("worldSealed"),SyncedWorld && SyncedWorld->IsSealed());
    Reply->SetStringField(TEXT("importId"),SyncedWorld ? SyncedWorld->GetImportId() : FString());
    Reply->SetNumberField(TEXT("importedCells"),SyncedWorld ? SyncedWorld->ImportedCells() : 0);
    Reply->SetNumberField(TEXT("textureMaterials"),TexturePalette ? TexturePalette->Materials.Num() : 0);
    Reply->SetNumberField(TEXT("worldCells"),SyncedWorld ? SyncedWorld->CellCount() : 0);
    Reply->SetNumberField(TEXT("worldShapes"),SyncedWorld ? SyncedWorld->ShapeCount() : 0);
    Reply->SetStringField(TEXT("blockModelError"),SyncedWorld ? SyncedWorld->GetModelError().Left(80) : FString());
    Reply->SetBoolField(TEXT("videoReady"),Video->Streaming);
    FString BudgetText;FJsonSerializer::Serialize(Reply,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&BudgetText));
    if(FTCHARToUTF8(*BudgetText).Length()>BridgeProtocol::MaxPacketBytes) {
        for(const TCHAR* Name:{TEXT("videoColor"),TEXT("heldModel"),TEXT("blockModelError")}) Reply->RemoveField(Name);
    }
    BudgetText.Empty();FJsonSerializer::Serialize(Reply,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&BudgetText));
    if(FTCHARToUTF8(*BudgetText).Length()>BridgeProtocol::MaxPacketBytes) {
        for(const TCHAR* Name:{TEXT("particles"),TEXT("lastAction"),TEXT("mobReason"),TEXT("textureMaterials"),TEXT("worldShapes"),TEXT("maskPixels"),TEXT("maskForeground"),TEXT("maskTranslucent")}) Reply->RemoveField(Name);
    }
    SendJson(Reply, Sender);
}
void ABridgeReceiver::ClearPreview(uint64 Generation) {
    PreviewGeneration = Generation; SnapshotCompleted = false; StagingId.Empty(); ExpectedBatches = 0; StagedBatches.Empty();
    if (IsValid(Preview)) Preview->Clear(); PreviewBlocks = 0;
}
bool ABridgeReceiver::HandleSnapshot(const FBridgePacket& P) {
    if (P.SnapshotSequence < PreviewGeneration) return true; // Late batches must not restore a cleared/older view.
    if (P.SnapshotSequence == PreviewGeneration && (SnapshotCompleted || StagingId.IsEmpty())) return true;
    if (P.SnapshotSequence > PreviewGeneration) {
        PreviewGeneration = P.SnapshotSequence; StagingId = P.SnapshotId; ExpectedBatches = P.TotalBatches;
        SnapshotCompleted = false; StagedBatches.Empty(); StagingDeadline = FPlatformTime::Seconds() + 15;
    }
    if (StagingId != P.SnapshotId || ExpectedBatches != P.TotalBatches) return false;
    if (!StagedBatches.Contains(P.BatchIndex)) StagedBatches.Add(P.BatchIndex, P.Blocks);
    if (StagedBatches.Num() != ExpectedBatches) return true;
    TArray<FBridgeBlock> Blocks; TSet<int32> Colors;
    for (int32 I = 0; I < ExpectedBatches; ++I) {
        const auto* Batch = StagedBatches.Find(I); if (!Batch) return false;
        Blocks.Append(*Batch);
    }
    for (const auto& Block : Blocks) Colors.Add(Block.Color);
    if (Blocks.Num() > BridgeProtocol::MaxPreviewBlocks || Colors.Num() > 64) return false;
    if (!IsValid(Preview)) Preview = GetWorld()->SpawnActor<ABridgeBlockPreview>();
    if (!Preview) return false;
    Preview->Replace(Blocks, Anchor, PreviewMaterial); PreviewBlocks = Blocks.Num(); SnapshotCompleted = true;
    StagedBatches.Empty(); StagingId.Empty(); ExpectedBatches = 0;
    UE_LOG(LogTemp, Display, TEXT("Bridge: applied complete preview (%d blocks)"), PreviewBlocks); return true;
}
void ABridgeReceiver::Explode(const FVector& Position) {
    if (ExplosionSystem) UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), ExplosionSystem, Position);
    LastExplosionWalls = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It) {
        if (!It->ActorHasTag(TEXT("BridgeWall"))) continue;
        TInlineComponentArray<UGeometryCollectionComponent*> Collections; It->GetComponents(Collections);
        for (UGeometryCollectionComponent* GC : Collections) {
            if (!GC->GetRestCollection() || FVector::Dist(GC->Bounds.GetBox().GetClosestPointTo(Position), Position) > ExplosionRadius) continue;
            URadialFalloff* Damage = NewObject<URadialFalloff>(this);
            Damage->SetRadialFalloff(Strain, 0.f, 1.f, 0.f, FMath::Max(1.f, ExplosionRadius), Position, EFieldFalloffType::Field_FallOff_None);
            GC->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain, nullptr, Damage);
            URadialVector* Impulse = NewObject<URadialVector>(this); Impulse->SetRadialVector(Force, Position);
            URadialFalloff* Mask = NewObject<URadialFalloff>(this);
            Mask->SetRadialFalloff(1.f, 0.f, 1.f, 0.f, FMath::Max(1.f, ExplosionRadius), Position, EFieldFalloffType::Field_FallOff_None);
            UOperatorField* LocalForce = NewObject<UOperatorField>(this);
            LocalForce->SetOperatorField(1.f, Mask, Impulse, EFieldOperationType::Field_Multiply);
            GC->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_LinearForce, nullptr, LocalForce); ++LastExplosionWalls;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Bridge TNT at %s, affected wall collections: %d"), *Position.ToString(), LastExplosionWalls);
    OnTntExplosion(Position);
}

int32 ABridgeReceiver::RemoveImportedBlocks(FVector Position,float Radius) {
    return IsValid(SyncedWorld) ? SyncedWorld->RemoveBlocksInSphere(Position,Radius) : 0;
}
void ABridgeReceiver::SendPose() {
    if(!TargetCharacter || !Peer.IsValid()) return;
    FVector Feet;
    if(const auto* Bridge=Cast<ABridgeCharacter>(TargetCharacter)) Feet=Bridge->GetMinecraftFeetPosition();
    else { Feet=TargetCharacter->GetActorLocation(); Feet.Z-=TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); }
    const FVector Relative=(Feet-Anchor)/100;
    auto Reply=MakeShared<FJsonObject>(); Reply->SetNumberField(TEXT("v"),1); Reply->SetStringField(TEXT("kind"),TEXT("pose"));
    Reply->SetStringField(TEXT("session"),Session); Reply->SetStringField(TEXT("receiverId"),InstanceId);
    Reply->SetNumberField(TEXT("seq"),double(LastSequence)); Reply->SetNumberField(TEXT("poseSeq"),double(++PoseSequence));
    Reply->SetNumberField(TEXT("x"),-Relative.Y); Reply->SetNumberField(TEXT("y"),Relative.Z); Reply->SetNumberField(TEXT("z"),Relative.X);
    Reply->SetBoolField(TEXT("grounded"),TargetCharacter->GetCharacterMovement()->IsMovingOnGround());
    if(auto* Bridge=Cast<ABridgeCharacter>(TargetCharacter)) Reply->SetBoolField(TEXT("flying"),Bridge->BridgeFlying);
    SendJson(Reply,Peer.ToSharedRef());
}

void ABridgeReceiver::BlockAction(const FBridgePacket& P) {
    auto* Character=Cast<ABridgeCharacter>(TargetCharacter);
    const double Now=FPlatformTime::Seconds();
    if(P.Sequence<=LastActionSequence) return;
    LastActionSequence=P.Sequence;
    if(!Character || !UEControl || (!NativePlayActive && (!Connected || Now-LastInput>.25)) || !IsValid(SyncedWorld) || P.ImportId!=SyncedWorld->GetImportId()) {LastAction=TEXT("controller not ready");return;}
    if(IsValid(MobWorld) && MobWorld->PlayerHealth<=0) {LastAction=TEXT("player dead; /uebridge respawn");return;}
    if(LastActionAt>=0 && Now-LastActionAt<.08) {LastAction=TEXT("rate limited");return;}
    LastActionAt=Now;Character->SwingHand();
    FVector EyePosition;FRotator EyeRotation;Character->GetEyeAim(EyePosition,EyeRotation);
    const float BlockReach=NativePlayActive && !NativeCreative ? 450.f : 500.f;
    const float AttackReach=NativePlayActive ? (NativeCreative ? 500.f : 300.f) : 500.f;
    const float AttackDamage=NativePlayActive ? NativeAttackDamage() : 4.f;
    if(!NativePlayActive && P.Action==TEXT("break") && IsValid(MobWorld) && MobWorld->Attack(EyePosition,BridgeProtocol::ToRotation(P.Yaw,P.Pitch).Vector(),AttackReach,AttackDamage)) {
        if(NativePlayActive) NativeLastAttack=GetWorld()->GetTimeSeconds();
        LastAction=TEXT("mob attacked");return;
    }
    FIntVector Block;FVector Normal,HitPoint;
    if(!SyncedWorld->Aim(EyePosition,BridgeProtocol::ToRotation(P.Yaw,P.Pitch),BlockReach,Block,Normal,Character,&HitPoint)) {LastAction=TEXT("no imported block in reach");return;}
    if(P.Action==TEXT("break")) {
        FString BrokenId;FColor BrokenTint;const bool Known=SyncedWorld->GetBlockInfo(Block,BrokenId,BrokenTint);
        if(NativePlayActive && !NativeCreative && Known &&
            (BrokenId==TEXT("minecraft:bedrock") || BrokenId==TEXT("minecraft:barrier") || BrokenId==TEXT("minecraft:end_portal_frame")
            || BrokenId==TEXT("minecraft:command_block") || BrokenId==TEXT("minecraft:chain_command_block") || BrokenId==TEXT("minecraft:repeating_command_block")
            || BrokenId==TEXT("minecraft:structure_block") || BrokenId==TEXT("minecraft:jigsaw"))) {
            LastAction=TEXT("unbreakable in survival");return;
        }
        TArray<FBox> DustBoxes;SyncedWorld->GetBlockOutline(Block,DustBoxes);
        const FVector Center=SyncedWorld->BlockCenter(Block);
        const bool Broken=SyncedWorld->BreakBlock(Block);LastAction=Broken ? TEXT("broken") : TEXT("no block");
        if(Broken && Known) {
            QueueFeedback(TEXT("break"),BrokenId,Center);
            if(NativePlayActive && !NativeCreative) SpawnNativeDrop(BrokenId,1,Center,FVector(0,0,160));
            if(IsValid(VanillaEffects)) {
                // Process() runs before the frame's character configuration; resolve
                // the current local palette/material even for the first break event.
                VanillaEffects->Configure(SyncedWorld,TexturePalette,VanillaParticleMaterial);
                VanillaEffects->SetViewCamera(Character->BridgeCamera);
                VanillaEffects->SpawnBreak(Center,BrokenId,BrokenTint,DustBoxes);
            }
        }
        return;
    }
    if(!P.Sneak && SyncedWorld->UseBlock(Block)) {LastAction=TEXT("block used");return;}
    if(!P.SpawnType.IsEmpty()) {
        if(!EnsureMobWorld()) {LastAction=TEXT("mob world unavailable");return;}
        const FVector Spawn=HitPoint+Normal*(FMath::Abs(Normal.Z)>.5 ? 2.f : 50.f);
        LastAction=MobWorld->SpawnEgg(P.SpawnType,Spawn,Anchor,P.EventId);return;
    }
    if(P.HeldBlock.IsEmpty()) {LastAction=TEXT("select a supported block or spawn egg");return;}
    FString HitId,HitState;
    if(P.HeldBlock.EndsWith(TEXT("_slab")) && SyncedWorld->GetBlockState(Block,HitId,HitState) && HitId==P.HeldBlock
        && ((HitState.Contains(TEXT("type=bottom")) && Normal.Z>.5) || (HitState.Contains(TEXT("type=top")) && Normal.Z<-.5))) {
        LastAction=SyncedWorld->PlaceBlock(Block,P.HeldBlock,P.HeldColor,P.Yaw,Normal,HitPoint);
        if(LastAction==TEXT("placed")) QueueFeedback(TEXT("place"),P.HeldBlock,SyncedWorld->BlockCenter(Block));
        return;
    }
    const FVector MinecraftNormal(-Normal.Y,Normal.Z,Normal.X);
    int32 Axis=0;if(FMath::Abs(MinecraftNormal.Y)>FMath::Abs(MinecraftNormal.X)) Axis=1;
    if(FMath::Abs(MinecraftNormal.Z)>FMath::Abs(MinecraftNormal[Axis])) Axis=2;
    FIntVector Adjacent=Block;Adjacent[Axis]+=MinecraftNormal[Axis]>=0 ? 1 : -1;
    LastAction=SyncedWorld->PlaceBlock(Adjacent,P.HeldBlock,P.HeldColor,P.Yaw,Normal,HitPoint);
    if(LastAction==TEXT("placed")) QueueFeedback(TEXT("place"),P.HeldBlock,SyncedWorld->BlockCenter(Adjacent));
}

void ABridgeReceiver::QueueFeedback(const FString& Type,const FString& BlockId,const FVector& Position,float FallDistance) {
    if(NativePlayActive) {
        FString Event;
        float Volume=1,Pitch=1;
        if(NativeStore.IsValid() && NativeStore->GetMetadata().Manifest.IsValid()) {
            const TSharedPtr<FJsonObject>* Groups=nullptr;
            if(NativeStore->GetMetadata().Manifest->TryGetObjectField(TEXT("blockSounds"),Groups)) {
                const TSharedPtr<FJsonObject>* Group=nullptr;
                if((*Groups)->TryGetObjectField(BlockId,Group)) {
                    (*Group)->TryGetStringField(Type==TEXT("land") ? TEXT("fall") : Type,Event);
                    double Number=0;if((*Group)->TryGetNumberField(TEXT("volume"),Number)) Volume=float(Number);
                    if((*Group)->TryGetNumberField(TEXT("pitch"),Number)) Pitch=float(Number);
                    if(Type==TEXT("open") || Type==TEXT("close") || Type==TEXT("activate") || Type==TEXT("deactivate")) {
                        Volume=1;Pitch=1;
                        if((*Group)->TryGetNumberField(TEXT("interactionVolume"),Number)) Volume=float(Number);
                        if((*Group)->TryGetNumberField(Type==TEXT("activate") ? TEXT("activatePitch") : TEXT("deactivatePitch"),Number)) Pitch=float(Number);
                        else {
                            double Min=.9,Max=1;
                            (*Group)->TryGetNumberField(TEXT("interactionPitchMin"),Min);(*Group)->TryGetNumberField(TEXT("interactionPitchMax"),Max);
                            Pitch=FMath::FRandRange(float(Min),float(Max));
                        }
                    }
                }
            }
        }
        if(Type==TEXT("land")) {
            if(FallDistance>3 && !NativeCreative && EnsureMobWorld()) {
                MobWorld->HitPlayer(FMath::CeilToFloat(FallDistance-3),Position);
                PlayNativeSound(FallDistance>4 ? TEXT("minecraft:entity.player.big_fall") : TEXT("minecraft:entity.player.small_fall"),Position,1,1,TEXT("player"));
            }
            Volume*=.5f;Pitch*=.75f;
        }
        else if(Type==TEXT("step")) Volume*=.15f;
        else if(Type==TEXT("break") || Type==TEXT("place")) {Volume=(Volume+1)*.5f;Pitch*=.8f;}
        if(!Event.IsEmpty()) PlayNativeSound(Event,Position,Volume,Pitch,Type==TEXT("step") || Type==TEXT("land") ? TEXT("player") : TEXT("block"));
        return;
    }
    if(!Connected || !UEControl || !Peer.IsValid() || PendingFeedback.Num()>=64 || BlockId.IsEmpty()) return;
    const FString Id=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    const FVector Relative=(Position-Anchor)/100;
    FMinimalViewInfo View;CastChecked<ABridgeCharacter>(TargetCharacter)->BridgeCamera->GetCameraView(0,View);
    const FVector Listener=(View.Location-Anchor)/100;
    auto Reply=MakeShared<FJsonObject>();Reply->SetNumberField(TEXT("v"),1);Reply->SetStringField(TEXT("kind"),TEXT("feedback"));
    Reply->SetStringField(TEXT("session"),Session);Reply->SetStringField(TEXT("receiverId"),InstanceId);Reply->SetStringField(TEXT("effectId"),Id);
    Reply->SetNumberField(TEXT("seq"),double(LastSequence));Reply->SetStringField(TEXT("type"),Type);Reply->SetStringField(TEXT("block"),BlockId);
    Reply->SetNumberField(TEXT("x"),-Relative.Y);Reply->SetNumberField(TEXT("y"),Relative.Z);Reply->SetNumberField(TEXT("z"),Relative.X);
    Reply->SetNumberField(TEXT("listenerX"),-Listener.Y);Reply->SetNumberField(TEXT("listenerY"),Listener.Z);Reply->SetNumberField(TEXT("listenerZ"),Listener.X);
    Reply->SetNumberField(TEXT("listenerYaw"),View.Rotation.Yaw);Reply->SetNumberField(TEXT("listenerPitch"),-View.Rotation.Pitch);
    Reply->SetNumberField(TEXT("fallDistance"),FallDistance);
    PendingFeedback.Add(Id,FPendingFeedback{Reply,FPlatformTime::Seconds(),-1});
}
void ABridgeReceiver::PumpFeedback(double Now) {
    for(auto It=PendingFeedback.CreateIterator();It;++It) {
        auto& Effect=It.Value();
        if(!Connected || !UEControl || Now-Effect.Created>1) {It.RemoveCurrent();continue;}
        if(Peer.IsValid() && (Effect.Sent<0 || Now-Effect.Sent>=.1)) {
            Effect.Json->SetNumberField(TEXT("seq"),double(LastSequence));
            SendJson(Effect.Json.ToSharedRef(),Peer.ToSharedRef());Effect.Sent=Now;
        }
    }
}
void ABridgeReceiver::QueueMobSound(const FString& Sound,const FVector& Position) {
    if(NativePlayActive) {
        FString Category=Sound.Contains(TEXT("entity.player.")) ? TEXT("player") : TEXT("neutral");
        if(MobPalette) for(const auto& Pair:MobPalette->Templates) {
            const FString Species=Pair.Key.Replace(TEXT("minecraft:"),TEXT(""));
            if(Sound.StartsWith(TEXT("minecraft:entity.")+Species+TEXT("."))) {
                if(const auto* Appearance=MobPalette->Find(Pair.Value)) Category=Appearance->Hostile ? TEXT("hostile") : TEXT("neutral");
                break;
            }
        }
        PlayNativeSound(Sound,Position,1,1,Category);return;
    }
    auto* Character=Cast<ABridgeCharacter>(TargetCharacter);
    if(!Character || !Connected || !UEControl || !Peer.IsValid() || PendingFeedback.Num()>=64 || Sound.IsEmpty()) return;
    FMinimalViewInfo View;Character->BridgeCamera->GetCameraView(0,View);
    const FVector Delta=(Position-View.Location)/100;const FRotationMatrix Basis(View.Rotation);
    const FString Id=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    auto Reply=MakeShared<FJsonObject>();Reply->SetNumberField(TEXT("v"),1);Reply->SetStringField(TEXT("kind"),TEXT("mob_feedback"));
    Reply->SetStringField(TEXT("session"),Session);Reply->SetStringField(TEXT("receiverId"),InstanceId);Reply->SetStringField(TEXT("effectId"),Id);
    Reply->SetNumberField(TEXT("seq"),double(LastSequence));Reply->SetStringField(TEXT("type"),TEXT("mob"));Reply->SetStringField(TEXT("sound"),Sound);
    Reply->SetNumberField(TEXT("lx"),FVector::DotProduct(Delta,Basis.GetUnitAxis(EAxis::Y)));
    Reply->SetNumberField(TEXT("ly"),FVector::DotProduct(Delta,Basis.GetUnitAxis(EAxis::Z)));
    Reply->SetNumberField(TEXT("lz"),FVector::DotProduct(Delta,Basis.GetUnitAxis(EAxis::X)));
    PendingFeedback.Add(Id,FPendingFeedback{Reply,FPlatformTime::Seconds(),-1});
}

bool ABridgeReceiver::EnsureMobWorld() {
    if(!IsValid(MobWorld)) {
        MobWorld=GetWorld()->SpawnActor<ABridgeMobWorld>();
        if(!MobWorld) return false;
        MobWorld->Sound=[this](const FString& Sound,const FVector& Position){QueueMobSound(Sound,Position);};
        MobWorld->PrepareSpawnCollision=[this](const FVector& Feet){if(IsValid(SyncedWorld)) SyncedWorld->EnsureCollisionForPosition(Feet);};
        MobWorld->SpawnAllowed=[this](const FVector& Feet){return IsValid(SyncedWorld) && SyncedWorld->ContainsUEPosition(Feet);};
        Video->AddTickPrerequisiteActor(MobWorld);
    }
    MobWorld->Palette=MobPalette;MobWorld->SetCreative(LatestInput.Creative);MobWorld->SetAuthority(UEControl,TargetCharacter);
    return true;
}
bool ABridgeReceiver::EnsureItemWorld() {
    if(!IsValid(ItemWorld)) {
        ItemWorld=GetWorld()->SpawnActor<ABridgeItemWorld>();
        if(!ItemWorld) return false;
        ItemWorld->Result=[this](const FString& Tx,int32 Revision,const FString& Action,int32 Count,const FString& Reason){QueueItemResult(Tx,Revision,Action,Count,Reason);};
        ItemWorld->Lighting=[this](ABridgeDroppedItem* Item){LightActor(Item);};
        ItemWorld->Contains=[this](const FVector& Position){return IsValid(SyncedWorld) && SyncedWorld->ContainsUEPosition(Position);};
        ItemWorld->PickupSound=[this](const FVector& Position){QueueMobSound(TEXT("minecraft:entity.item.pickup"),Position);};
        Video->AddTickPrerequisiteActor(ItemWorld);
    }
    ItemWorld->AddTickPrerequisiteActor(this);
    if(IsValid(SyncedWorld)) ItemWorld->AddTickPrerequisiteActor(SyncedWorld);
    ItemWorld->Palette=TexturePalette;ItemWorld->SetNativeLocal(NativePlayActive);ItemWorld->SetAuthority(UEControl,TargetCharacter);return true;
}
void ABridgeReceiver::QueueItemResult(const FString& Tx,int32 Revision,const FString& Action,int32 Count,const FString& Reason) {
    if(NativePlayActive) {
        if(!IsValid(ItemWorld)) return;
        if(Revision==0) {ItemWorld->Resolve(Tx,0,0);return;}
        if(NativeDropRevisions.FindRef(Tx)>=Revision) return;
        NativeDropRevisions.Add(Tx,Revision);
        int32 Accepted=0;
        if(Action==TEXT("pickup")) {
            auto* Controller=Cast<ABridgeNativePlayerController>(UGameplayStatics::GetPlayerController(this,0));
            auto* Inventory=Controller ? Controller->GetNativeInventory() : nullptr;
            if(const auto* Item=NativeDropItems.Find(Tx)) if(Inventory) Accepted=Count-Inventory->InsertStack(*Item,Count);
        }
        ItemWorld->Resolve(Tx,Revision,Accepted);
        return;
    }
    if(!Peer.IsValid() || Session.IsEmpty() || LastSequence==0) return;
    auto Reply=MakeShared<FJsonObject>();Reply->SetNumberField(TEXT("v"),1);Reply->SetStringField(TEXT("kind"),TEXT("item_result"));
    Reply->SetStringField(TEXT("session"),Session);Reply->SetStringField(TEXT("receiverId"),InstanceId);Reply->SetNumberField(TEXT("seq"),double(LastSequence));
    Reply->SetStringField(TEXT("itemTx"),Tx);Reply->SetNumberField(TEXT("itemRevision"),Revision);Reply->SetStringField(TEXT("itemAction"),Action);
    Reply->SetNumberField(TEXT("itemCount"),Count);Reply->SetStringField(TEXT("itemReason"),Reason.Left(160));SendJson(Reply,Peer.ToSharedRef());
}
void ABridgeReceiver::LightActor(AActor* Actor) {
    if(!Actor || !IsValid(SyncedWorld) || !SyncedWorld->GetLighting()) return;
    const FVector Relative=(Actor->GetActorLocation()-Anchor)/100.0;
    const FVector Source=SourceOrigin+FVector(-Relative.Y,Relative.Z,Relative.X);
    SyncedWorld->GetLighting()->ApplyActor(Actor,FIntVector(FMath::FloorToInt(Source.X),FMath::FloorToInt(Source.Y),FMath::FloorToInt(Source.Z)));
}
void ABridgeReceiver::SendPerformance() {
    if(!Peer.IsValid()) return;
    auto Reply=MakeShared<FJsonObject>();Reply->SetNumberField(TEXT("v"),1);Reply->SetStringField(TEXT("kind"),TEXT("perf_status"));
    Reply->SetStringField(TEXT("session"),Session);Reply->SetStringField(TEXT("receiverId"),InstanceId);Reply->SetNumberField(TEXT("seq"),double(LastSequence));
    // Frame intervals averaged across the reporting window; these are not CPU/GPU execution timings.
    Reply->SetNumberField(TEXT("fps"),PerformanceSeconds>0 ? PerformanceFrames/PerformanceSeconds : 0);
    Reply->SetNumberField(TEXT("frameMs"),PerformanceFrames>0 ? PerformanceSeconds*1000.0/PerformanceFrames : 0);
    PerformanceFrames=0;PerformanceSeconds=0;
    Reply->SetNumberField(TEXT("faces"),IsValid(SyncedWorld) ? SyncedWorld->RenderedFaceCount() : 0);
    Reply->SetNumberField(TEXT("renderSections"),IsValid(SyncedWorld) ? SyncedWorld->RenderSectionCount() : 0);
    Reply->SetNumberField(TEXT("cells"),IsValid(SyncedWorld) ? SyncedWorld->CellCount() : 0);
    Reply->SetNumberField(TEXT("streamPending"),IsValid(SyncedWorld) ? SyncedWorld->RebuildPending() : 0);
    Reply->SetNumberField(TEXT("lightPending"),IsValid(SyncedWorld) && SyncedWorld->GetLighting() ? SyncedWorld->GetLighting()->Pending() : 0);
    Reply->SetNumberField(TEXT("itemCount"),IsValid(ItemWorld) ? ItemWorld->AliveCount() : 0);
    Reply->SetStringField(TEXT("videoTransport"),Video->GetTransportName());Reply->SetStringField(TEXT("gpuReason"),Video->GetGpuDiagnostic().Left(160));
    Reply->SetNumberField(TEXT("videoReadyMs"),Video->GetCaptureMs());Reply->SetNumberField(TEXT("videoDropped"),Video->GetDroppedFrames());
    SendJson(Reply,Peer.ToSharedRef());
}
