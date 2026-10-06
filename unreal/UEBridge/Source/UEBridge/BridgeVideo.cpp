#include "BridgeVideo.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Async/Async.h"
#include "GameFramework/Actor.h"
#include "RHI.h"

namespace {
void Word(TArray<uint8>& Bytes,uint32 V) { Bytes.Add(uint8(V>>24)); Bytes.Add(uint8(V>>16)); Bytes.Add(uint8(V>>8)); Bytes.Add(uint8(V)); }
}
UBridgeVideo::UBridgeVideo() { PrimaryComponentTick.bCanEverTick=false; }
void UBridgeVideo::Start(int32 Port) {
    if (Listener || Port<1024 || Port>65535) return;
    ISocketSubsystem* S=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); if (!S) return;
    bool Valid=false; auto Address=S->CreateInternetAddr(); Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(Port);
    Listener=S->CreateSocket(NAME_Stream,TEXT("UEBridgeVideo"),false);
    if (!Listener || !Valid || !Listener->SetNonBlocking(true) || !Listener->Bind(*Address) || !Listener->Listen(1)) {
        UE_LOG(LogTemp,Error,TEXT("Bridge video: cannot listen on 127.0.0.1:%d"),Port);
        if (Listener) { Listener->Close(); S->DestroySocket(Listener); Listener=nullptr; } return;
    }
    FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    UE_LOG(LogTemp,Display,TEXT("Bridge 0.4.0 video listening on 127.0.0.1:%d (TCP)"),Port);
}
void UBridgeVideo::DropClient() {
    if (Client) { Client->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Client); Client=nullptr; }
    Streaming=false; ClientSession.Empty(); Hello.Empty(); Output.Empty(); Sent=0;
}
void UBridgeVideo::EndPlay(const EEndPlayReason::Type Reason) {
    DropClient();
    if (Listener) { Listener->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Listener); Listener=nullptr; }
    if (Encoding.IsValid()) Encoding.Wait(); // Worker owns pixels only; no UObject is touched by it.
    if (Capture) Capture->DestroyComponent(); Capture=nullptr; Target=nullptr;
    Super::EndPlay(Reason);
}
void UBridgeVideo::Flush() {
    if (!Client || Output.IsEmpty()) return;
    // Bound work on the game thread; a slow consumer cannot accumulate frames.
    int32 Count=0;
    if (Client->Send(Output.GetData()+Sent,FMath::Min(Output.Num()-Sent,1024*1024),Count)) {
        if (Count>0) { Sent+=Count; LastProgress=FPlatformTime::Seconds(); }
        if (Sent==Output.Num()) { Output.Empty(); Sent=0; }
    } else if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode()!=SE_EWOULDBLOCK) DropClient();
    if (Client && !Output.IsEmpty() && FPlatformTime::Seconds()-LastProgress>2) DropClient();
}
void UBridgeVideo::TickStream(UCameraComponent* Camera,const FString& Session) {
    if (!Listener) return;
    ISocketSubsystem* S=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); const double Now=FPlatformTime::Seconds();
    if (Client && (Session.IsEmpty() || (!ClientSession.IsEmpty() && ClientSession!=Session))) DropClient();
    bool Pending=false;
    if (Listener->HasPendingConnection(Pending) && Pending) {
        auto Peer=S->CreateInternetAddr(); FSocket* Accepted=Listener->Accept(*Peer,TEXT("UEBridgeVideoClient"));
        uint32 Ip=0; Peer->GetIp(Ip);
        if (Accepted) {
            if (Client || Ip!=0x7f000001 || Session.IsEmpty() || !Accepted->SetNonBlocking(true)) { Accepted->Close(); S->DestroySocket(Accepted); }
            else { Client=Accepted; Client->SetNoDelay(true); AcceptedAt=LastProgress=Now; Hello.Empty(); }
        }
    }
    if (Client && ClientSession.IsEmpty()) {
        uint8 Bytes[40]; int32 Count=0;
        if (Client->Recv(Bytes,40-Hello.Num(),Count)) {
            if (Count==0) { DropClient(); return; } Hello.Append(Bytes,Count);
        } else if (S->GetLastErrorCode()!=SE_EWOULDBLOCK) { DropClient(); return; }
        if (Hello.Num()==40) {
            FTCHARToUTF8 Guid(*Session);
            if (Guid.Length()!=36 || FMemory::Memcmp(Hello.GetData(),"UEBH",4)!=0 || FMemory::Memcmp(Hello.GetData()+4,Guid.Get(),36)!=0) { DropClient(); return; }
            ClientSession=Session; Streaming=true; LastCapture=-1;
        } else if (Now-AcceptedAt>2) { DropClient(); return; }
    }
    if (Encoding.IsValid() && Encoding.IsReady()) {
        TArray<uint8> Frame=Encoding.Get(); Encoding=TFuture<TArray<uint8>>();
        if (Streaming && ClientSession==EncodeSession && Output.IsEmpty()) { Output=MoveTemp(Frame); Sent=0; LastProgress=Now; }
    }
    Flush();
    if (!Streaming || !Camera || Encoding.IsValid() || !Output.IsEmpty()
        || (LastCapture>=0 && Now-LastCapture<1.0/FMath::Clamp(FramesPerSecond,1,30))) return;
    const int32 W=FMath::Clamp(Width,160,1920), H=FMath::Clamp(Height,90,1080);
    if (!Capture) {
        Capture=NewObject<USceneCaptureComponent2D>(GetOwner());
        Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
        Capture->bAlwaysPersistRenderingState=true; // Keep exposure/TAA history between explicit captures.
        Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR; Capture->RegisterComponent();
    }
    if (!Target || Target->SizeX!=W || Target->SizeY!=H) {
        Target=NewObject<UTextureRenderTarget2D>(this);
        Target->ClearColor=FLinearColor::Black; Target->TargetGamma=2.2f;
        Target->InitCustomFormat(W,H,PF_B8G8R8A8,false); Capture->TextureTarget=Target;
    }
    FMinimalViewInfo View; Camera->GetCameraView(0,View);
    Capture->SetWorldLocationAndRotation(View.Location,View.Rotation); Capture->FOVAngle=View.FOV;
    Capture->PostProcessSettings=View.PostProcessSettings; Capture->PostProcessBlendWeight=View.PostProcessBlendWeight;
    if (!FMath::IsNearlyZero(ExposureCompensation)) {
        Capture->PostProcessSettings.bOverride_AutoExposureBias=true;
        Capture->PostProcessSettings.AutoExposureBias=View.PostProcessSettings.AutoExposureBias+FMath::Clamp(ExposureCompensation,-6.f,6.f);
        Capture->PostProcessBlendWeight=1;
    }
    Capture->CaptureScene();
    TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
    if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags) || Pixels.Num()!=W*H) return;
    LastCapture=Now; EncodeSession=Session; const uint32 FrameSequence=++Sequence; const int32 Q=FMath::Clamp(Quality,30,95);
    IImageWrapperModule* Images=&FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    Encoding=Async(EAsyncExecution::ThreadPool,[Pixels=MoveTemp(Pixels),W,H,Q,FrameSequence,Images]() {
        TArray<uint8> Frame; auto Jpeg=Images->CreateImageWrapper(EImageFormat::JPEG);
        if (!Jpeg || !Jpeg->SetRaw(Pixels.GetData(),int64(Pixels.Num())*sizeof(FColor),W,H,ERGBFormat::BGRA,8)) return Frame;
        const auto& Bytes=Jpeg->GetCompressed(Q); if (Bytes.Num()<4 || Bytes.Num()>2*1024*1024) return Frame;
        Word(Frame,0x55454256); Word(Frame,1); Word(Frame,W); Word(Frame,H); Word(Frame,FrameSequence); Word(Frame,uint32(Bytes.Num()));
        Frame.Append(Bytes.GetData(),int32(Bytes.Num())); return Frame;
    });
}
