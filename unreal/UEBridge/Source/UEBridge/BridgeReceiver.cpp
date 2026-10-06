#include "BridgeReceiver.h"
#include "BridgeBlockPreview.h"
#include "BridgeArrow.h"
#include "BridgeCharacter.h"
#include "BridgeWorld.h"
#include "BridgeVideo.h"
#include "BridgeBlockPalette.h"
#include "BridgePlayerAppearance.h"
#include "BridgeVanillaEffects.h"
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

ABridgeReceiver::ABridgeReceiver() {
    PrimaryActorTick.bCanEverTick = true;
    Video=CreateDefaultSubobject<UBridgeVideo>(TEXT("Video"));
}
void ABridgeReceiver::AcquireTarget() {
    if (!IsValid(TargetCharacter)) { TargetCharacter = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0)); Anchored = false; }
    if (TargetCharacter && !Anchored) {
        Anchor = TargetCharacter->GetActorLocation();
        Anchor.Z -= TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
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
    if(IsValid(VanillaEffects)) Video->AddTickPrerequisiteActor(VanillaEffects);
    UE_LOG(LogTemp, Display, TEXT("Bridge 0.8.0 listening on 127.0.0.1:%d"), Port);
    Video->Start(VideoPort);
    if (!TargetCharacter) UE_LOG(LogTemp, Warning, TEXT("Bridge: waiting for player Character; will retry every tick"));
    if (!ExplosionSystem) UE_LOG(LogTemp, Warning, TEXT("Bridge: ExplosionSystem is unset; Niagara will not play"));
}
void ABridgeReceiver::EndPlay(const EEndPlayReason::Type Reason) {
    if (IsValid(Preview)) Preview->Destroy();
    if (IsValid(SyncedWorld)) SyncedWorld->Destroy();
    if (IsValid(VanillaEffects)) VanillaEffects->Destroy();
    PendingFeedback.Empty();
    for (auto& Arrow : Arrows) if (IsValid(Arrow)) Arrow->Destroy();
    if (Socket) { Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); Socket = nullptr; }
    Super::EndPlay(Reason);
}
void ABridgeReceiver::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds); AcquireTarget();
    Arrows.RemoveAll([](const auto& Arrow) { return !IsValid(Arrow); });
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
        Bridge->SetAuthorityEnabled(UEControl);
        Bridge->ConfigureVisuals(PreviewMaterial,TexturePalette,LatestInput.HeldItem,LatestInput.HeldBlock,LatestInput.HeldColor);
        Bridge->ConfigureAppearance(PlayerAppearance);
        Bridge->SetMinecraftFov(float(LatestInput.CameraFov));
        Bridge->ApplyPlayerVisuals(LatestInput.Perspective,float(LatestInput.SwingProgress),float(LatestInput.EquipProgress),LatestInput.UsingItem,
            LatestInput.UseAction,float(LatestInput.UseProgress),LatestInput.LeftHanded,LatestInput.SkinLayers,LatestInput.SlimArms);
        Bridge->SetInteractionWorld(SyncedWorld);
        if(UEControl) {
            if(auto* C=Bridge->GetController()) C->SetControlRotation(BridgeProtocol::ToRotation(LatestInput.Yaw,LatestInput.Pitch));
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
    PumpFeedback(Now);
    if(Sealed && Peer.IsValid() && (LastPose<0 || Now-LastPose>=1.0/60)) { LastPose=Now; SendPose(); }
    Video->SetSource(TargetCharacter ? TargetCharacter->FindComponentByClass<UCameraComponent>() : nullptr, Connected ? Session : FString(),LastSequence);
    for (auto It = SeenEvents.CreateIterator(); It; ++It) if (Now - It.Value() > 10) It.RemoveCurrent();
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
    if (Session != P.Session || PeerPort != Sender->GetPort() || PeerAddress != SenderIp) {
        if (!Session.IsEmpty() && Now - LastPacket < 0.5) return;
        Session = P.Session; PeerPort = Sender->GetPort(); PeerAddress = SenderIp;
        LastSequence = 0; LastInput = 0; LastStatus = -1; SeenEvents.Empty();PendingFeedback.Empty();HasNewInput = false;
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
        LatestInput = P; HasNewInput = true;
        if (LastStatus < 0 || Now - LastStatus >= 0.25) { LastStatus = Now; SendStatus(Sender); }
        return;
    }
    if (!Anchored) return; // Retry the event after the player/spawn anchor becomes available.
    if (!SeenEvents.Contains(P.EventId)) {
        if (SeenEvents.Num() >= 2048) return;
        const FVector Position = BridgeProtocol::ToUnreal(P.Position, Anchor);
        switch (P.Kind) {
            case EBridgeKind::BlockAction: BlockAction(P);break;
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
                Video->Quality=P.VideoQuality; Video->ExposureCompensation=float(P.VideoExposure); break;
            case EBridgeKind::WorldBegin: {
                if(!Cast<ABridgeCharacter>(TargetCharacter)) return;
                if(!IsValid(SyncedWorld)) SyncedWorld=GetWorld()->SpawnActor<ABridgeWorld>();
                if(!SyncedWorld) return;
                const bool ReplaceImport=SyncedWorld->GetImportId()!=P.ImportId;
                if(!SyncedWorld->BeginImport(P,Anchor)) return;
                if(ReplaceImport && SyncedWorld->GetImportId()==P.ImportId) {
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
    }
    LastPacket = Now;
    auto Ack = MakeShared<FJsonObject>(); Ack->SetNumberField(TEXT("v"), 1); Ack->SetStringField(TEXT("kind"), TEXT("ack"));
    Ack->SetStringField(TEXT("session"), Session); Ack->SetStringField(TEXT("eventId"), P.EventId); SendJson(Ack, Sender);
}
void ABridgeReceiver::SendJson(const TSharedRef<FJsonObject>& Json, const TSharedRef<FInternetAddr>& Sender) {
    FString Text; FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    FTCHARToUTF8 Utf8(*Text); int32 Sent = 0;
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
    Reply->SetStringField(TEXT("build"),TEXT("0.8.0"));
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
    ParticleStatus->SetStringField(TEXT("lastBlock"),Dust.LastBlock);
    ParticleStatus->SetNumberField(TEXT("lastRequested"),Dust.LastRequested);
    ParticleStatus->SetNumberField(TEXT("lastSpawned"),Dust.LastSpawned);
    ParticleStatus->SetStringField(TEXT("lastReason"),Dust.LastReason);
    Reply->SetObjectField(TEXT("particles"),ParticleStatus);
    Reply->SetStringField(TEXT("lastAction"),LastAction);
    Reply->SetBoolField(TEXT("authorityV1"),Cast<ABridgeCharacter>(TargetCharacter)!=nullptr);
    Reply->SetBoolField(TEXT("ueControl"),UEControl);
    Reply->SetBoolField(TEXT("worldSealed"),SyncedWorld && SyncedWorld->IsSealed());
    Reply->SetStringField(TEXT("importId"),SyncedWorld ? SyncedWorld->GetImportId() : FString());
    Reply->SetNumberField(TEXT("importedCells"),SyncedWorld ? SyncedWorld->ImportedCells() : 0);
    Reply->SetNumberField(TEXT("textureMaterials"),TexturePalette ? TexturePalette->Materials.Num() : 0);
    Reply->SetNumberField(TEXT("worldCells"),SyncedWorld ? SyncedWorld->CellCount() : 0);
    Reply->SetNumberField(TEXT("worldShapes"),SyncedWorld ? SyncedWorld->ShapeCount() : 0);
    Reply->SetBoolField(TEXT("videoReady"),Video->Streaming); SendJson(Reply, Sender);
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
    SendJson(Reply,Peer.ToSharedRef());
}

void ABridgeReceiver::BlockAction(const FBridgePacket& P) {
    auto* Character=Cast<ABridgeCharacter>(TargetCharacter);
    const double Now=FPlatformTime::Seconds();
    if(P.Sequence<=LastActionSequence) return;
    LastActionSequence=P.Sequence;
    if(!Character || !UEControl || !Connected || Now-LastInput>.25 || !IsValid(SyncedWorld) || P.ImportId!=SyncedWorld->GetImportId()) {LastAction=TEXT("controller not ready");return;}
    if(LastActionAt>=0 && Now-LastActionAt<.08) {LastAction=TEXT("rate limited");return;}
    LastActionAt=Now;Character->SwingHand();
    FVector EyePosition;FRotator EyeRotation;Character->GetEyeAim(EyePosition,EyeRotation);
    FIntVector Block;FVector Normal;
    if(!SyncedWorld->Aim(EyePosition,BridgeProtocol::ToRotation(P.Yaw,P.Pitch),500.f,Block,Normal,Character)) {LastAction=TEXT("no imported block in reach");return;}
    if(P.Action==TEXT("break")) {
        FString BrokenId;FColor BrokenTint;const bool Known=SyncedWorld->GetBlockInfo(Block,BrokenId,BrokenTint);
        const FVector Center=SyncedWorld->BlockCenter(Block);
        const bool Broken=SyncedWorld->BreakBlock(Block);LastAction=Broken ? TEXT("broken") : TEXT("no block");
        if(Broken && Known) {
            QueueFeedback(TEXT("break"),BrokenId,Center);
            if(IsValid(VanillaEffects)) {
                // Process() runs before the frame's character configuration; resolve
                // the current local palette/material even for the first break event.
                VanillaEffects->Configure(SyncedWorld,TexturePalette,VanillaParticleMaterial);
                VanillaEffects->SetViewCamera(Character->BridgeCamera);
                VanillaEffects->SpawnBreak(Center,BrokenId,BrokenTint);
            }
        }
        return;
    }
    if(P.HeldBlock.IsEmpty()) {LastAction=TEXT("select a full cube block");return;}
    const FVector MinecraftNormal(-Normal.Y,Normal.Z,Normal.X);
    int32 Axis=0;if(FMath::Abs(MinecraftNormal.Y)>FMath::Abs(MinecraftNormal.X)) Axis=1;
    if(FMath::Abs(MinecraftNormal.Z)>FMath::Abs(MinecraftNormal[Axis])) Axis=2;
    FIntVector Adjacent=Block;Adjacent[Axis]+=MinecraftNormal[Axis]>=0 ? 1 : -1;
    LastAction=SyncedWorld->PlaceBlock(Adjacent,P.HeldBlock,P.HeldColor);
    if(LastAction==TEXT("placed")) QueueFeedback(TEXT("place"),P.HeldBlock,SyncedWorld->BlockCenter(Adjacent));
}

void ABridgeReceiver::QueueFeedback(const FString& Type,const FString& BlockId,const FVector& Position,float FallDistance) {
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
