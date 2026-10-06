#include "BridgeReceiver.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Field/FieldSystemObjects.h"
#include "EngineUtils.h"

ABridgeReceiver::ABridgeReceiver() { PrimaryActorTick.bCanEverTick = true; }
void ABridgeReceiver::BeginPlay() {
    Super::BeginPlay();
    if (!TargetCharacter) TargetCharacter = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (TargetCharacter) {
        Anchor = TargetCharacter->GetActorLocation();
        Anchor.Z -= TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TargetCharacter->GetCharacterMovement()->DisableMovement();
    }
    ISocketSubsystem* S = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    Socket = S->CreateSocket(NAME_DGram, TEXT("MinecraftBridge"), false);
    bool Valid = false;
    auto Address = S->CreateInternetAddr(); Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(Port);
    if (!Socket || !Valid || !Socket->SetNonBlocking(true) || !Socket->Bind(*Address)) {
        UE_LOG(LogTemp, Error, TEXT("Bridge: cannot bind 127.0.0.1:%d"), Port);
        if (Socket) { S->DestroySocket(Socket); Socket = nullptr; }
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("Bridge listening on 127.0.0.1:%d"), Port);
    if (!TargetCharacter) UE_LOG(LogTemp, Error, TEXT("Bridge: assign TargetCharacter or possess BridgeCharacter before BeginPlay"));
    if (!ExplosionSystem) UE_LOG(LogTemp, Warning, TEXT("Bridge: ExplosionSystem is unset; Niagara will not play"));
}
void ABridgeReceiver::EndPlay(const EEndPlayReason::Type Reason) {
    if (Socket) { Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); Socket = nullptr; }
    Super::EndPlay(Reason);
}
void ABridgeReceiver::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!Socket) return;
    uint8 Bytes[2049];
    for (int32 I = 0; I < 256; ++I) {
        uint32 Pending = 0; if (!Socket->HasPendingData(Pending)) break;
        int32 Count = 0; auto Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        if (!Socket->RecvFrom(Bytes, 2049, Count, *Sender) || Count <= 0 || Count > 2048) continue;
        uint32 SenderIp = 0; Sender->GetIp(SenderIp);
        if (SenderIp != 0x7f000001) continue;
        FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes), Count);
        FString Json(Text.Length(), Text.Get()); TSharedPtr<FJsonObject> Packet;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Packet) && Packet.IsValid()) Process(Packet, Sender);
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - LastInput > 0.25) { ForwardInput = RightInput = 0; JumpHeld = false; }
    for (auto It = SeenEvents.CreateIterator(); It; ++It) if (Now - It.Value() > 10) It.RemoveCurrent();
}
void ABridgeReceiver::Process(const TSharedPtr<FJsonObject>& P, const TSharedRef<FInternetAddr>& Sender) {
    double Version, Seq, X, Y, Z; FString Kind, IncomingSession;
    if (!P->TryGetNumberField(TEXT("v"), Version) || Version != 1
        || !P->TryGetStringField(TEXT("kind"), Kind)
        || (Kind != TEXT("input") && Kind != TEXT("event"))
        || !P->TryGetStringField(TEXT("session"), IncomingSession) || IncomingSession.Len() != 36
        || !P->TryGetNumberField(TEXT("seq"), Seq) || !FMath::IsFinite(Seq) || Seq < 1 || Seq > 9007199254740991.0 || FMath::FloorToDouble(Seq) != Seq
        || !P->TryGetNumberField(TEXT("x"), X) || !P->TryGetNumberField(TEXT("y"), Y) || !P->TryGetNumberField(TEXT("z"), Z)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)
        || FMath::Abs(X) > 100000 || FMath::Abs(Y) > 100000 || FMath::Abs(Z) > 100000) return;
    FGuid SessionGuid;
    if (!FGuid::Parse(IncomingSession, SessionGuid)) return;
    const double Now = FPlatformTime::Seconds();
    // One live source. A new session may replace it after 500ms of silence.
    uint32 SenderIp = 0; Sender->GetIp(SenderIp);
    if (Session != IncomingSession || PeerPort != Sender->GetPort() || PeerAddress != SenderIp) {
        if (!Session.IsEmpty() && Now - LastPacket < 0.5) return;
        Session = IncomingSession; PeerPort = Sender->GetPort(); PeerAddress = SenderIp;
        LastSequence = 0; SeenEvents.Empty();
        // Keep the original UE spawn anchor across MC world/reconnect sessions.
    }
    LastPacket = Now;
    const FVector Position = Anchor + FVector(Z, X, Y) * 100.f;
    if (Kind == TEXT("input")) {
        double Yaw, Pitch, Forward, Right; bool Jump;
        if (!P->TryGetNumberField(TEXT("yaw"), Yaw) || !P->TryGetNumberField(TEXT("pitch"), Pitch)
            || !P->TryGetNumberField(TEXT("forward"), Forward) || !P->TryGetNumberField(TEXT("right"), Right)
            || !P->TryGetBoolField(TEXT("jump"), Jump)
            || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Pitch) || FMath::Abs(Yaw) > 1e9 || FMath::Abs(Pitch) > 90
            || !FMath::IsFinite(Forward) || !FMath::IsFinite(Right) || FMath::Abs(Forward) > 1 || FMath::Abs(Right) > 1
            || uint64(Seq) <= LastSequence) return;
        LastSequence = uint64(Seq); LastInput = Now;
        ForwardInput = Forward; RightInput = Right;
        if (Jump && !JumpHeld) OnJumpPressed(); JumpHeld = Jump;
        if (TargetCharacter) {
            const FVector CapsuleOffset(0, 0, TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            TargetCharacter->SetActorLocation(Position + CapsuleOffset, false, nullptr, ETeleportType::TeleportPhysics);
            TargetCharacter->SetActorRotation(FRotator(0, -Yaw, 0));
            if (AController* C = TargetCharacter->GetController()) C->SetControlRotation(FRotator(-Pitch, -Yaw, 0));
        }
    } else {
        FString Event, Id;
        if (!P->TryGetStringField(TEXT("event"), Event) || Event != TEXT("tnt_ignite")
            || !P->TryGetStringField(TEXT("eventId"), Id) || Id.Len() != 36) return;
        FGuid EventGuid;
        if (!FGuid::Parse(Id, EventGuid)) return;
        if (!SeenEvents.Contains(Id)) {
            if (SeenEvents.Num() >= 256) return;
            SeenEvents.Add(Id, Now); Explode(Position);
        }
        const FString Ack = FString::Printf(TEXT("{\"v\":1,\"kind\":\"ack\",\"session\":\"%s\",\"eventId\":\"%s\"}"), *Session, *Id);
        // Validate UUIDs before formatting ACK (no arbitrary JSON string interpolation).
        FTCHARToUTF8 Utf8(*Ack); int32 Sent; Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, *Sender);
    }
}
void ABridgeReceiver::Explode(const FVector& Position) {
    if (ExplosionSystem) UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), ExplosionSystem, Position);
    int32 Hit = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It) {
        // Only tagged MVP walls, never arbitrary scene physics objects.
        if (!It->ActorHasTag(TEXT("BridgeWall"))) continue;
        TInlineComponentArray<UGeometryCollectionComponent*> Collections; It->GetComponents(Collections);
        for (UGeometryCollectionComponent* GC : Collections) {
            if (FVector::Dist(GC->Bounds.GetBox().GetClosestPointTo(Position), Position) > ExplosionRadius) continue;
            URadialFalloff* Damage = NewObject<URadialFalloff>(this);
            Damage->SetRadialFalloff(Strain, 0.f, 1.f, 0.f, ExplosionRadius, Position, EFieldFalloffType::Field_FallOff_None);
            GC->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain, nullptr, Damage);
            URadialVector* Impulse = NewObject<URadialVector>(this); Impulse->SetRadialVector(Force, Position);
            URadialFalloff* Mask = NewObject<URadialFalloff>(this);
            Mask->SetRadialFalloff(1.f, 0.f, 1.f, 0.f, ExplosionRadius, Position, EFieldFalloffType::Field_FallOff_None);
            UOperatorField* LocalForce = NewObject<UOperatorField>(this);
            LocalForce->SetOperatorField(1.f, Mask, Impulse, EFieldOperationType::Field_Multiply);
            GC->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_LinearForce, nullptr, LocalForce);
            ++Hit;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Bridge TNT at %s, affected wall collections: %d"), *Position.ToString(), Hit);
    OnTntExplosion(Position);
}
