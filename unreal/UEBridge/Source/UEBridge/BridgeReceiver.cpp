#include "BridgeReceiver.h"
#include "BridgeBlockPreview.h"
#include "BridgeArrow.h"
#include "BridgeCharacter.h"
#include "BridgeWorld.h"
#include "BridgeVideo.h"
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
    UE_LOG(LogTemp, Display, TEXT("Bridge 0.3.0 listening on 127.0.0.1:%d"), Port);
    Video->Start(VideoPort);
    if (!TargetCharacter) UE_LOG(LogTemp, Warning, TEXT("Bridge: waiting for player Character; will retry every tick"));
    if (!ExplosionSystem) UE_LOG(LogTemp, Warning, TEXT("Bridge: ExplosionSystem is unset; Niagara will not play"));
}
void ABridgeReceiver::EndPlay(const EEndPlayReason::Type Reason) {
    if (IsValid(Preview)) Preview->Destroy();
    if (IsValid(SyncedWorld)) SyncedWorld->Destroy();
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
    if (HasNewInput && TargetCharacter) {
        if (auto* Bridge=Cast<ABridgeCharacter>(TargetCharacter)) Bridge->ApplyMinecraftPose(LatestInput.BodyHeight,LatestInput.EyeHeight,LatestInput.Sneak);
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
    Video->TickStream(TargetCharacter ? TargetCharacter->FindComponentByClass<UCameraComponent>() : nullptr, Connected ? Session : FString());
    for (auto It = SeenEvents.CreateIterator(); It; ++It) if (Now - It.Value() > 10) It.RemoveCurrent();
    if (!StagedBatches.IsEmpty() && Now > StagingDeadline) {
        StagedBatches.Empty(); StagingId.Empty(); ExpectedBatches = 0;
        UE_LOG(LogTemp, Warning, TEXT("Bridge: incomplete preview snapshot expired; previous view retained"));
    }
}
void ABridgeReceiver::Process(const FBridgePacket& P, const TSharedRef<FInternetAddr>& Sender) {
    const double Now = FPlatformTime::Seconds(); uint32 SenderIp = 0; Sender->GetIp(SenderIp);
    if (Session != P.Session || PeerPort != Sender->GetPort() || PeerAddress != SenderIp) {
        if (!Session.IsEmpty() && Now - LastPacket < 0.5) return;
        Session = P.Session; PeerPort = Sender->GetPort(); PeerAddress = SenderIp;
        LastSequence = 0; LastInput = 0; LastStatus = -1; SeenEvents.Empty(); HasNewInput = false;
        JumpHeld = SneakHeld = false; ForwardInput = RightInput = 0; ClearPreview(0);
        if (IsValid(SyncedWorld)) SyncedWorld->Clear();
    }
    if (P.Kind == EBridgeKind::Input) {
        if (P.Sequence <= LastSequence) return;
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
            case EBridgeKind::Tnt: Explode(Position); break;
            case EBridgeKind::Bow: {
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
            case EBridgeKind::WorldCell:
            case EBridgeKind::WorldScope:
            case EBridgeKind::WorldClear:
                if (!IsValid(SyncedWorld)) SyncedWorld=GetWorld()->SpawnActor<ABridgeWorld>();
                if (!SyncedWorld || !SyncedWorld->Handle(P,Anchor,PreviewMaterial)) return;
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
    Reply->SetStringField(TEXT("build"),TEXT("0.3.0"));
    Reply->SetStringField(TEXT("receiverId"),InstanceId);
    Reply->SetBoolField(TEXT("worldV1"),true); Reply->SetBoolField(TEXT("videoV1"),true);
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
