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
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include <atomic>

// Render-thread-owned RHI readback. Shared captures keep it alive through queued commands.
struct FBridgeGpuFrame {
    TUniquePtr<FRHIGPUTextureReadback> Readback;
    std::atomic<bool> Polling{false}, Done{false};
    TArray<FColor> Pixels;
    FString Session;
    int32 Width=0,Height=0,Quality=85;
    uint32 Sequence=0;
    uint64 Input=0;
    double CapturedAt=0,ReadbackMs=0;
};

namespace {
void Word(TArray<uint8>& Bytes,uint32 V) { Bytes.Add(uint8(V>>24)); Bytes.Add(uint8(V>>16)); Bytes.Add(uint8(V>>8)); Bytes.Add(uint8(V)); }
}
UBridgeVideo::UBridgeVideo() {
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UBridgeVideo::SetSource(UCameraComponent* Camera,const FString& Session,uint64 InputSequence) {
    SourceCamera=Camera;SourceSession=Session;SourceInput=InputSequence;
}
void UBridgeVideo::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    TickStream(SourceCamera.Get(),SourceSession,SourceInput);
}
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
    UE_LOG(LogTemp,Display,TEXT("Bridge 0.6.0 video listening on 127.0.0.1:%d (TCP)"),Port);
}
void UBridgeVideo::DropClient() {
    if (Client) { Client->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Client); Client=nullptr; }
    Streaming=false; ClientSession.Empty(); Hello.Empty(); Output.Empty(); Sent=0;
}
void UBridgeVideo::EndPlay(const EEndPlayReason::Type Reason) {
    DropClient();
    if (Listener) { Listener->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Listener); Listener=nullptr; }
    if (Encoding.IsValid()) Encoding.Wait(); // Worker owns pixels only; no UObject is touched by it.
    // Only shutdown waits for render commands. No FlushRenderingCommands/ReadPixels occurs per frame.
    if(!Readbacks.IsEmpty()) {
        ENQUEUE_RENDER_COMMAND(BridgeFinishReadbacks)([Frames=Readbacks](FRHICommandListImmediate& RHICmdList) {RHICmdList.BlockUntilGPUIdle();});
    }
    FlushRenderingCommands();Readbacks.Empty();
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
void UBridgeVideo::TickStream(UCameraComponent* Camera,const FString& Session,uint64 InputSequence) {
    if (!Listener) return;
    ISocketSubsystem* S=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); const double Now=FPlatformTime::Seconds();
    if (Client && (Session.IsEmpty() || (!ClientSession.IsEmpty() && ClientSession!=Session))) DropClient();
    bool Pending=false;
    if (Listener->HasPendingConnection(Pending) && Pending) {
        auto Peer=S->CreateInternetAddr(); FSocket* Accepted=Listener->Accept(*Peer,TEXT("UEBridgeVideoClient"));
        uint32 Ip=0; Peer->GetIp(Ip);
        if (Accepted) {
            if (Client || Ip!=0x7f000001 || Session.IsEmpty() || !Accepted->SetNonBlocking(true)) { Accepted->Close(); S->DestroySocket(Accepted); }
            else { Client=Accepted; Client->SetNoDelay(true);int32 ActualBuffer=0;Client->SetSendBufferSize(64*1024,ActualBuffer); AcceptedAt=LastProgress=Now; Hello.Empty(); }
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
    // Poll fences on the render thread; never wait for the GPU on the game thread.
    for(const auto& State:Readbacks) if(!State->Done.load() && !State->Polling.exchange(true)) {
        ENQUEUE_RENDER_COMMAND(BridgePollPixels)([State](FRHICommandListImmediate& RHICmdList) {
            if(State->Readback && State->Readback->IsReady()) {
                int32 RowPitch=0,BufferHeight=0;
                const auto* Data=static_cast<const FColor*>(State->Readback->Lock(RowPitch,&BufferHeight));
                if(Data && RowPitch>=State->Width && BufferHeight>=State->Height) {
                    State->Pixels.SetNumUninitialized(State->Width*State->Height);
                    for(int32 Y=0;Y<State->Height;++Y) FMemory::Memcpy(State->Pixels.GetData()+Y*State->Width,Data+Y*RowPitch,State->Width*sizeof(FColor));
                }
                if(Data) State->Readback->Unlock();
                State->ReadbackMs=(FPlatformTime::Seconds()-State->CapturedAt)*1000;
                State->Done.store(true);
            }
            State->Polling.store(false);
        });
    }
    if(!Encoding.IsValid() && Output.IsEmpty()) {
        TSharedPtr<FBridgeGpuFrame,ESPMode::ThreadSafe> Ready;
        for(const auto& State:Readbacks) if(State->Done.load() && (!Ready || State->Sequence>Ready->Sequence)) Ready=State;
        Readbacks.RemoveAll([&](const auto& State){return State->Done.load() && (!Ready || State->Sequence<=Ready->Sequence);});
        if(Ready && Streaming && Ready->Session==ClientSession && Ready->Pixels.Num()==Ready->Width*Ready->Height && Now-Ready->CapturedAt<.25) {
            EncodeSession=Ready->Session;
            IImageWrapperModule* Images=&FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            Encoding=Async(EAsyncExecution::ThreadPool,[Ready,Images]() {
                TArray<uint8> Frame;const double EncodeAt=FPlatformTime::Seconds();
                auto Jpeg=Images->CreateImageWrapper(EImageFormat::JPEG);
                if(!Jpeg || !Jpeg->SetRaw(Ready->Pixels.GetData(),int64(Ready->Pixels.Num())*sizeof(FColor),Ready->Width,Ready->Height,ERGBFormat::BGRA,8)) return Frame;
                const auto& Bytes=Jpeg->GetCompressed(Ready->Quality);if(Bytes.Num()<4 || Bytes.Num()>2*1024*1024) return Frame;
                const double EncodeMs=(FPlatformTime::Seconds()-EncodeAt)*1000;
                Word(Frame,0x55454256);Word(Frame,2);Word(Frame,Ready->Width);Word(Frame,Ready->Height);Word(Frame,Ready->Sequence);Word(Frame,uint32(Bytes.Num()));
                Word(Frame,uint32(Ready->Input>>32));Word(Frame,uint32(Ready->Input));
                Word(Frame,uint32(FMath::Clamp(Ready->ReadbackMs*1000,0.0,10000000.0)));
                Word(Frame,uint32(FMath::Clamp(EncodeMs*1000,0.0,10000000.0)));
                Frame.Append(Bytes.GetData(),int32(Bytes.Num()));return Frame;
            });
        }
    }
    if(!Streaming || !Camera || Readbacks.Num()>=2 || !Output.IsEmpty()
        || (LastCapture>=0 && Now-LastCapture<1.0/FMath::Clamp(FramesPerSecond,1,60))) return;
    const int32 W=FMath::Clamp(Width,160,1920),H=FMath::Clamp(Height,90,1080);
    if(!Capture) {
        Capture=NewObject<USceneCaptureComponent2D>(GetOwner());
        Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
        Capture->bAlwaysPersistRenderingState=true;
        Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;Capture->RegisterComponent();
    }
    if(!Target || Target->SizeX!=W || Target->SizeY!=H) {
        Target=NewObject<UTextureRenderTarget2D>(this);Target->ClearColor=FLinearColor::Black;Target->TargetGamma=2.2f;
        Target->InitCustomFormat(W,H,PF_B8G8R8A8,false);Capture->TextureTarget=Target;
    }
    FMinimalViewInfo View;Camera->GetCameraView(0,View);
    Capture->SetWorldLocationAndRotation(View.Location,View.Rotation);Capture->FOVAngle=View.FOV;
    Capture->PostProcessSettings=View.PostProcessSettings;Capture->PostProcessBlendWeight=View.PostProcessBlendWeight;
    if(!FMath::IsNearlyZero(ExposureCompensation)) {
        Capture->PostProcessSettings.bOverride_AutoExposureBias=true;
        Capture->PostProcessSettings.AutoExposureBias=View.PostProcessSettings.AutoExposureBias+FMath::Clamp(ExposureCompensation,-6.f,6.f);
        Capture->PostProcessBlendWeight=1;
    }
    Capture->CaptureScene();LastCapture=Now;
    auto State=MakeShared<FBridgeGpuFrame,ESPMode::ThreadSafe>();
    State->Width=W;State->Height=H;State->Quality=FMath::Clamp(Quality,30,95);State->Sequence=++Sequence;
    State->Session=Session;State->Input=InputSequence;State->CapturedAt=Now;Readbacks.Add(State);
    // Hold the RHI texture across a resolution change. CaptureScene and copy use render queue order.
    FTextureRHIRef Texture=Target->GameThread_GetRenderTargetResource()->GetRenderTargetTexture();
    ENQUEUE_RENDER_COMMAND(BridgeReadPixelsAsync)([State,Texture](FRHICommandListImmediate& RHICmdList) {
        if(!Texture.IsValid()) {State->Done.store(true);return;}
        State->Readback=MakeUnique<FRHIGPUTextureReadback>(TEXT("UEBridgeFrame"));
        State->Readback->EnqueueCopy(RHICmdList,Texture.GetReference());
    });
}
