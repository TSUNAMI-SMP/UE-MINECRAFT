#include "BridgeVideo.h"
#include "BridgeVideoMask.h"
#include "BridgeSharedGpu.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
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
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "ShowFlags.h"
#include "Math/Float16Color.h"
#include "DynamicRHI.h"
#include "Misc/ScopeLock.h"
#include <atomic>

// Render-thread-owned RHI readback. Shared captures keep it alive through queued commands.
struct FBridgeGpuFrame;
struct FBridgeSharedTransport {
    TUniquePtr<BridgeSharedGpu::Producer> Producer;
    TMap<uint32,TWeakPtr<FBridgeGpuFrame,ESPMode::ThreadSafe>> Pending;
    std::atomic<bool> Failed{false},Initialized{false};
    FCriticalSection DiagnosticLock;
    FString Diagnostic=TEXT("Waiting for D3D11 shared textures");
    int32 Width=0,Height=0;
};
struct FBridgeGpuFrame {
    TUniquePtr<FRHIGPUTextureReadback> Readback;
    std::atomic<bool> Polling{false}, Done{false};
    TArray<FFloat16Color> LinearPixels;
    TArray<FColor> Pixels;
    TArray<uint8> Opacity;
    FString Session;
    int32 Width=0,Height=0,Quality=85;
    uint32 Sequence=0;
    uint32 Revision=0;
    bool V3=false,HasMask=false;
    int32 Foreground=0,Translucent=0;
    FVector MCCamera=FVector::ZeroVector;
    float Yaw=0,Pitch=0,VerticalFov=80;
    uint64 Input=0;
    double CapturedAt=0,ReadbackMs=0;
    bool GPU=false;
    BridgeSharedGpu::Frame Shared;
    TSharedPtr<FBridgeSharedTransport,ESPMode::ThreadSafe> Transport;
};

namespace {
void Word(TArray<uint8>& Bytes,uint32 V) { Bytes.Add(uint8(V>>24)); Bytes.Add(uint8(V>>16)); Bytes.Add(uint8(V>>8)); Bytes.Add(uint8(V)); }
void Real(TArray<uint8>& Bytes,double V) {uint64 Bits=0;FMemory::Memcpy(&Bits,&V,sizeof(Bits));Word(Bytes,uint32(Bits>>32));Word(Bytes,uint32(Bits));}
void Single(TArray<uint8>& Bytes,float V) {uint32 Bits=0;FMemory::Memcpy(&Bits,&V,sizeof(Bits));Word(Bytes,Bits);}
uint32 ReadWord(const uint8* Bytes){return (uint32(Bytes[0])<<24)|(uint32(Bytes[1])<<16)|(uint32(Bytes[2])<<8)|Bytes[3];}
void Header(TArray<uint8>& Frame,const FBridgeGpuFrame& Ready,int32 Version,int32 Payload,int32 MaskBytes) {
    Word(Frame,0x55454256);Word(Frame,Version);Word(Frame,Ready.Width);Word(Frame,Ready.Height);Word(Frame,Ready.Sequence);Word(Frame,Payload);
    Word(Frame,uint32(Ready.Input>>32));Word(Frame,uint32(Ready.Input));
    Word(Frame,uint32(FMath::Clamp(Ready.ReadbackMs*1000,0.0,10000000.0)));
    Word(Frame,0);
    if(Version>=3) {
        Word(Frame,(Ready.HasMask ? 1 : 0)|(Ready.GPU ? 2 : 0));Word(Frame,MaskBytes);
        Real(Frame,Ready.MCCamera.X);Real(Frame,Ready.MCCamera.Y);Real(Frame,Ready.MCCamera.Z);
        Single(Frame,Ready.Yaw);Single(Frame,Ready.Pitch);Single(Frame,Ready.VerticalFov);
    }
}
TArray<uint8> MaskRuns(const TArray<uint8>& Opacity) {
    TArray<uint8> Bytes;
    BridgeVideoMask::Encode(Opacity.GetData(),std::size_t(Opacity.Num()),[&Bytes](uint8 Byte){Bytes.Add(Byte);});
    return Bytes;
}
void SharedFailure(const TSharedPtr<FBridgeSharedTransport,ESPMode::ThreadSafe>& Pool) {
    if(!Pool || !Pool->Producer || !Pool->Producer->Failed()) return;
    Pool->Failed.store(true);
    {FScopeLock Lock(&Pool->DiagnosticLock);Pool->Diagnostic=UTF8_TO_TCHAR(Pool->Producer->Error().c_str());}
    for(const auto& Pending:Pool->Pending) if(const auto Frame=Pending.Value.Pin()) Frame->Done.store(true);
    Pool->Pending.Empty();
}
}
UBridgeVideo::UBridgeVideo() {
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UBridgeVideo::SetRenderMode(bool Lighting,bool VanillaSky) {
    // Vanilla sky replaces UE's background only in the unlit mode.
    VanillaSky=VanillaSky && !Lighting;
    if(LightingEnabled==Lighting && VanillaSkyEnabled==VanillaSky) return;
    LightingEnabled=Lighting;VanillaSkyEnabled=VanillaSky;++ModeRevision;LastSkyScan=-1;
    CaptureSchedule.Reset();
    ResetSharedGpu();
    if(Capture) Capture->bCameraCutThisFrame=true;
    LastMaskPixels=LastMaskForeground=LastMaskTranslucent=0;
}
void UBridgeVideo::ConfigureCapture(USceneCaptureComponent2D* Component,bool Mask) {
    if(!Component) return;
    // Use the engine's complete view-mode flags, including diffuse/specular and
    // reflection settings, instead of relying on one lighting flag alone.
    // Vanilla materials provide their own emissive lightmap. Editor Unlit adds a
    // BRDF/specular preview term, so both modes keep Lit with explicit show flags.
    ApplyViewMode(VMI_Lit,true,Component->ShowFlags);
    Component->ShowFlags.SetLighting(LightingEnabled);
    Component->ShowFlags.SetDynamicShadows(LightingEnabled);
    Component->ShowFlags.SetPostProcessing(!Mask && LightingEnabled);
    Component->ShowFlags.SetSpecular(LightingEnabled);
    Component->ShowFlags.SetEyeAdaptation(LightingEnabled);
    Component->ShowFlags.SetGlobalIllumination(LightingEnabled);
    Component->ShowFlags.SetAmbientOcclusion(LightingEnabled);
    Component->ShowFlags.SetReflectionEnvironment(LightingEnabled);
    Component->ShowFlags.SetMotionBlur(false);
    const bool Sky=!VanillaSkyEnabled || !ClientV3;
    Component->ShowFlags.SetAtmosphere(Sky);Component->ShowFlags.SetFog(Sky);
    Component->ShowFlags.SetVolumetricFog(Sky);Component->ShowFlags.SetCloud(Sky);
    Component->ShowFlags.SetSkyLighting(LightingEnabled);
    // Independent temporal histories would give color/mask different edges. Sky mode uses matched non-temporal captures.
    Component->ShowFlags.SetAntiAliasing(false);
    // The HDR capture's alpha is inverse opacity, including opaque and translucent geometry.
    Component->bConsiderUnrenderedOpaquePixelAsFullyTranslucent=Mask;
}
void UBridgeVideo::RefreshHiddenSky() {
    Capture->HiddenComponents.Reset();
    if(!VanillaSkyEnabled || !ClientV3) return;
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        TInlineComponentArray<UPrimitiveComponent*> Parts;It->GetComponents(Parts);
        for(UPrimitiveComponent* Part:Parts) {
            bool Sky=It->ActorHasTag(TEXT("UEBridgeSky"));
            for(int32 I=0;!Sky && I<Part->GetNumMaterials();++I) {
                if(UMaterialInterface* Material=Part->GetMaterial(I)) if(UMaterial* Base=Material->GetMaterial()) Sky=Base->bIsSky;
            }
            if(Sky) Capture->HideComponent(Part);
        }
    }
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
    UE_LOG(LogTemp,Display,TEXT("Bridge 0.11.0 video listening on 127.0.0.1:%d (JPEG/GPU control TCP)"),Port);
}
FString UBridgeVideo::GetTransportName() const {
    return ClientGpu && SharedGpu && SharedGpu->Initialized.load() && !SharedGpu->Failed.load() ? TEXT("D3D11 GPU shared") : TEXT("JPEG TCP");
}
FString UBridgeVideo::GetGpuDiagnostic() const {
    if(!ClientGpu) return TEXT("Client selected JPEG or does not support NVIDIA interop");
    if(!SharedGpu) return TEXT("Waiting for D3D11 initialization");
    FScopeLock Lock(&SharedGpu->DiagnosticLock);return SharedGpu->Diagnostic;
}
void UBridgeVideo::ResetSharedGpu() {
    if(SharedGpu) {
        // Native D3D11 destruction stays on the render thread. Queued captures retain their own owner.
        ENQUEUE_RENDER_COMMAND(BridgeReleaseSharedPool)([Pool=SharedGpu](FRHICommandListImmediate&){});
        SharedGpu.Reset();
    }
}
void UBridgeVideo::UpdateStandaloneViewport(bool CaptureOnly) {
    if(!CaptureOnly) {
        if(auto* Viewport=StandaloneViewport.Get()) Viewport->bDisableWorldRendering=SavedViewportDisabled;
        StandaloneViewport.Reset();return;
    }
    if(StandaloneViewport.IsValid() || !GetWorld() || GetWorld()->WorldType!=EWorldType::Game) return;
    if(auto* Viewport=GetWorld()->GetGameViewport()) {
        StandaloneViewport=Viewport;SavedViewportDisabled=Viewport->bDisableWorldRendering;
        Viewport->bDisableWorldRendering=true;
        UE_LOG(LogTemp,Display,TEXT("Bridge video: standalone main viewport world rendering paused; Minecraft shows the capture. Editor PIE preview is unchanged."));
    }
}
void UBridgeVideo::ReadGpuAcknowledgements() {
    if(!Client || !ClientGpu) return;
    uint8 Bytes[256];int32 Count=0;
    if(Client->Recv(Bytes,sizeof(Bytes),Count)) {
        if(Count==0){DropClient();return;}Acknowledgements.Append(Bytes,Count);
    } else if(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode()!=SE_EWOULDBLOCK){DropClient();return;}
    if(Acknowledgements.Num()>512){DropClient();return;}
    int32 Offset=0;
    while(Acknowledgements.Num()-Offset>=16) {
        const uint8* Ack=Acknowledgements.GetData()+Offset;
        const uint32 Magic=ReadWord(Ack),FrameSequence=ReadWord(Ack+4),Slot=ReadWord(Ack+8),Generation=ReadWord(Ack+12);
        if(Magic==0x55454246) { // UEBF: native interop failed; preserve the authenticated session and resume JPEG.
            ClientGpu=false;ResetSharedGpu();++ModeRevision;CaptureSchedule.Reset();
            UE_LOG(LogTemp,Warning,TEXT("Bridge video: client requested JPEG fallback"));
        } else if(Magic==0x55454241 && Slot<3 && Generation!=0) {
            if(SharedGpu) {
                ENQUEUE_RENDER_COMMAND(BridgeSharedFrameReleased)([Pool=SharedGpu,FrameSequence,Slot,Generation](FRHICommandListImmediate&) {
                    if(Pool->Producer) Pool->Producer->Release(FrameSequence,Slot,Generation);
                });
            }
        } else {DropClient();return;}
        Offset+=16;
    }
    if(Offset) Acknowledgements.RemoveAt(0,Offset,EAllowShrinking::No);
}
void UBridgeVideo::DropClient() {
    if (Client) { Client->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Client); Client=nullptr; }
    Streaming=false;ClientV3=ClientGpu=false;ClientSession.Empty();Hello.Empty();Output.Empty();Acknowledgements.Empty();Sent=0;
    UpdateStandaloneViewport(false);
    ResetSharedGpu();
    ++ModeRevision;LastSkyScan=-1; // A worker from a previous handshake must never feed the next client.
    LastMaskPixels=LastMaskForeground=LastMaskTranslucent=0;
}
void UBridgeVideo::EndPlay(const EEndPlayReason::Type Reason) {
    DropClient();
    if (Listener) { Listener->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Listener); Listener=nullptr; }
    if (Encoding.IsValid()) Encoding.Wait(); // Worker owns pixels only; no UObject is touched by it.
    // Only shutdown waits for render commands. No FlushRenderingCommands/ReadPixels occurs per frame.
    if(!Readbacks.IsEmpty()) {
        ENQUEUE_RENDER_COMMAND(BridgeFinishReadbacks)([Frames=Readbacks](FRHICommandListImmediate& RHICmdList) {RHICmdList.SubmitAndBlockUntilGPUIdle();});
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
            ClientGpu=FMemory::Memcmp(Hello.GetData(),"UEB5",4)==0;
            ClientV3=ClientGpu || FMemory::Memcmp(Hello.GetData(),"UEB3",4)==0;
            if (Guid.Length()!=36 || (!ClientV3 && FMemory::Memcmp(Hello.GetData(),"UEBH",4)!=0) || FMemory::Memcmp(Hello.GetData()+4,Guid.Get(),36)!=0) { DropClient(); return; }
            ClientSession=Session;Streaming=true;CaptureSchedule.Reset();
            LastSkyScan=-1;
        } else if (Now-AcceptedAt>2) { DropClient(); return; }
    }
    if(Streaming) {ReadGpuAcknowledgements();UpdateStandaloneViewport(Streaming);}
    if (Encoding.IsValid() && Encoding.IsReady()) {
        TArray<uint8> Frame=Encoding.Get(); Encoding=TFuture<TArray<uint8>>();
        if (Streaming && ClientSession==EncodeSession && ModeRevision==EncodeRevision && Output.IsEmpty()) { Output=MoveTemp(Frame); Sent=0; LastProgress=Now; }
    }
    Flush();
    // Poll fences on the render thread; never wait for the GPU on the game thread.
    for(const auto& State:Readbacks) if(!State->Done.load() && !State->Polling.exchange(true)) {
        ENQUEUE_RENDER_COMMAND(BridgePollPixels)([State](FRHICommandListImmediate& RHICmdList) {
            if(State->GPU) {
                RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
                const auto& Pool=State->Transport;
                if(Pool && Pool->Producer) {
                    BridgeSharedGpu::Frame Frame;
                    while(Pool->Producer->TakeReady(Frame)) {
                        const auto* Weak=Pool->Pending.Find(Frame.Sequence);
                        const auto Ready=Weak ? Weak->Pin() : TSharedPtr<FBridgeGpuFrame,ESPMode::ThreadSafe>();
                        if(Ready) {
                            Ready->Shared=Frame;Ready->ReadbackMs=(FPlatformTime::Seconds()-Ready->CapturedAt)*1000;
                            Ready->Done.store(true);
                        } else Pool->Producer->Release(Frame.Sequence,Frame.Slot,Frame.Generation);
                        Pool->Pending.Remove(Frame.Sequence);
                    }
                    SharedFailure(Pool);
                }
                State->Polling.store(false);return;
            }
            if(State->Readback && State->Readback->IsReady()) {
                int32 RowPitch=0,BufferHeight=0;
                const auto* Data=static_cast<const FFloat16Color*>(State->Readback->Lock(RowPitch,&BufferHeight));
                if(Data && RowPitch>=State->Width && BufferHeight>=State->Height) {
                    State->LinearPixels.SetNumUninitialized(State->Width*State->Height);
                    for(int32 Y=0;Y<State->Height;++Y) FMemory::Memcpy(State->LinearPixels.GetData()+Y*State->Width,Data+Y*RowPitch,State->Width*sizeof(FFloat16Color));
                }
                if(Data) State->Readback->Unlock();
                if(State->HasMask) {
                    const auto* Mask=State->LinearPixels.GetData();RowPitch=State->Width;BufferHeight=State->Height;
                    if(Mask && RowPitch>=State->Width && BufferHeight>=State->Height) {
                        State->Opacity.SetNumUninitialized(State->Width*State->Height);
                        for(int32 Y=0;Y<State->Height;++Y) for(int32 X=0;X<State->Width;++X) {
                            const float InverseOpacity=Mask[Y*RowPitch+X].A.GetFloat();
                            State->Opacity[Y*State->Width+X]=FMath::IsFinite(InverseOpacity)
                                ? uint8(FMath::Clamp(FMath::RoundToInt((1.f-InverseOpacity)*255.f),0,255)) : 0;
                            const uint8 Alpha=State->Opacity[Y*State->Width+X];
                            if(Alpha>0) ++State->Foreground;if(Alpha>0 && Alpha<255) ++State->Translucent;
                        }
                    }

                }
                State->ReadbackMs=(FPlatformTime::Seconds()-State->CapturedAt)*1000;
                State->Done.store(true);
            }
            State->Polling.store(false);
        });
    }
    if(!Encoding.IsValid() && Output.IsEmpty()) {
        TSharedPtr<FBridgeGpuFrame,ESPMode::ThreadSafe> Ready;
        for(const auto& State:Readbacks) if(State->Done.load() && (!Ready || State->Sequence>Ready->Sequence)) Ready=State;
        for(const auto& State:Readbacks) if(State->Done.load() && State!=Ready) {
            ++DroppedFrames;
            if(State->GPU && State->Shared.Handle && State->Transport) {
                ENQUEUE_RENDER_COMMAND(BridgeDiscardSharedFrame)([State](FRHICommandListImmediate&) {
                    if(State->Transport->Producer) State->Transport->Producer->Release(State->Shared.Sequence,State->Shared.Slot,State->Shared.Generation);
                });
            }
        }
        Readbacks.RemoveAll([&](const auto& State){return State->Done.load() && (!Ready || State->Sequence<=Ready->Sequence);});
        if(Ready && Ready->GPU) {
            if(Streaming && Ready->Revision==ModeRevision && Ready->Session==ClientSession && Ready->Shared.Handle && Now-Ready->CapturedAt<.25) {
                Header(Output,*Ready,5,0,0);
                Word(Output,uint32(Ready->Shared.Handle>>32));Word(Output,uint32(Ready->Shared.Handle));
                Word(Output,uint32(Ready->Shared.Adapter>>32));Word(Output,uint32(Ready->Shared.Adapter));
                Word(Output,Ready->Shared.Slot);Word(Output,Ready->Shared.Generation);
                LastCaptureMs=float(Ready->ReadbackMs);Sent=0;LastProgress=Now;
                // Counts require reading pixels back; shared frames intentionally report unknown (-1).
                LastMaskPixels=Ready->HasMask ? Ready->Width*Ready->Height : 0;
                LastMaskForeground=LastMaskTranslucent=Ready->HasMask ? -1 : 0;
                // A shared frame is just a 108-byte notification. Send it now;
                // retaining it until the next tick would halve capture cadence
                // even when the socket and consumer have ample capacity.
                Flush();
            } else {
                ++DroppedFrames;
                if(Ready->Shared.Handle && Ready->Transport) {
                    ENQUEUE_RENDER_COMMAND(BridgeDiscardStaleSharedFrame)([Ready](FRHICommandListImmediate&) {
                        if(Ready->Transport->Producer) Ready->Transport->Producer->Release(Ready->Shared.Sequence,Ready->Shared.Slot,Ready->Shared.Generation);
                    });
                }
            }
        } else if(Ready && Streaming && Ready->Revision==ModeRevision && Ready->Session==ClientSession && Ready->LinearPixels.Num()==Ready->Width*Ready->Height
            && (!Ready->HasMask || Ready->Opacity.Num()==Ready->LinearPixels.Num()) && Now-Ready->CapturedAt<.25) {
            EncodeSession=Ready->Session;
            EncodeRevision=Ready->Revision;
            LastMaskPixels=Ready->HasMask ? Ready->Opacity.Num() : 0;
            LastMaskForeground=Ready->Foreground;LastMaskTranslucent=Ready->Translucent;
            LastCaptureMs=float(Ready->ReadbackMs);
            IImageWrapperModule* Images=&FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            Encoding=Async(EAsyncExecution::ThreadPool,[Ready,Images]() {
                TArray<uint8> Frame;const double EncodeAt=FPlatformTime::Seconds();
                // FinalToneCurveHDR is already tone mapped but still linear sRGB. Encode exactly once.
                // Unlit SceneColorHDR carries inverse opacity in the SAME capture, avoiding mismatched edges/history.
                Ready->Pixels.SetNumUninitialized(Ready->LinearPixels.Num());
                for(int32 I=0;I<Ready->LinearPixels.Num();++I) {
                    const auto& Pixel=Ready->LinearPixels[I];
                    const float Alpha=Ready->HasMask ? Ready->Opacity[I]/255.f : 1.f;
                    const float Weight=Alpha>0 ? 1.f/Alpha : 0.f;
                    const FLinearColor Color(FMath::Max(0.f,Pixel.R.GetFloat()*Weight),FMath::Max(0.f,Pixel.G.GetFloat()*Weight),FMath::Max(0.f,Pixel.B.GetFloat()*Weight),1);
                    Ready->Pixels[I]=Color.ToFColor(true);
                }
                auto Jpeg=Images->CreateImageWrapper(EImageFormat::JPEG);
                if(!Jpeg || !Jpeg->SetRaw(Ready->Pixels.GetData(),int64(Ready->Pixels.Num())*sizeof(FColor),Ready->Width,Ready->Height,ERGBFormat::BGRA,8)) return Frame;
                const auto& Bytes=Jpeg->GetCompressed(Ready->Quality);if(Bytes.Num()<4 || Bytes.Num()>2*1024*1024) return Frame;
                TArray<uint8> Mask=Ready->HasMask ? MaskRuns(Ready->Opacity) : TArray<uint8>();
                const double EncodeMs=(FPlatformTime::Seconds()-EncodeAt)*1000;
                Word(Frame,0x55454256);Word(Frame,Ready->V3 ? 4 : 2);Word(Frame,Ready->Width);Word(Frame,Ready->Height);Word(Frame,Ready->Sequence);Word(Frame,uint32(Bytes.Num()));
                Word(Frame,uint32(Ready->Input>>32));Word(Frame,uint32(Ready->Input));
                Word(Frame,uint32(FMath::Clamp(Ready->ReadbackMs*1000,0.0,10000000.0)));
                Word(Frame,uint32(FMath::Clamp(EncodeMs*1000,0.0,10000000.0)));
                if(Ready->V3) {
                    Word(Frame,Ready->HasMask ? 1 : 0);Word(Frame,uint32(Mask.Num()));
                    Real(Frame,Ready->MCCamera.X);Real(Frame,Ready->MCCamera.Y);Real(Frame,Ready->MCCamera.Z);
                    Single(Frame,Ready->Yaw);Single(Frame,Ready->Pitch);Single(Frame,Ready->VerticalFov);
                }
                Frame.Append(Bytes.GetData(),int32(Bytes.Num()));Frame.Append(Mask);return Frame;
            });
        }
    }
    const bool Share=ClientGpu && (!SharedGpu || !SharedGpu->Failed.load());
    if(!Streaming || !Camera || Readbacks.Num()>=(Share?3:2) || !Output.IsEmpty()) return;
    if(!CaptureSchedule.Capture(Now,FMath::Clamp(FramesPerSecond,1,60))) return;
    const int32 W=FMath::Clamp(Width,160,1920),H=FMath::Clamp(Height,90,1080);
    if(!Capture) {
        Capture=NewObject<USceneCaptureComponent2D>(GetOwner());
        Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
        Capture->bAlwaysPersistRenderingState=true;
        Capture->CaptureSource=ESceneCaptureSource::SCS_FinalToneCurveHDR;Capture->RegisterComponent();
    }
    if(!Target || Target->SizeX!=W || Target->SizeY!=H) {
        ResetSharedGpu();
        Target=NewObject<UTextureRenderTarget2D>(this);Target->ClearColor=FLinearColor::Black;Target->TargetGamma=1.f;
        Target->InitCustomFormat(W,H,PF_FloatRGBA,true);Capture->TextureTarget=Target;
    }
    const bool Mask=VanillaSkyEnabled && ClientV3;
    ConfigureCapture(Capture,false);
    Capture->CaptureSource=LightingEnabled ? ESceneCaptureSource::SCS_FinalToneCurveHDR : ESceneCaptureSource::SCS_SceneColorHDR;
    Capture->bConsiderUnrenderedOpaquePixelAsFullyTranslucent=Mask;
    if(LastSkyScan<0 || Now-LastSkyScan>1) {RefreshHiddenSky();LastSkyScan=Now;}
    FMinimalViewInfo View;Camera->GetCameraView(0,View);
    Capture->SetWorldLocationAndRotation(View.Location,View.Rotation);Capture->FOVAngle=View.FOV;
    Capture->PostProcessSettings=View.PostProcessSettings;Capture->PostProcessBlendWeight=View.PostProcessBlendWeight;
    Capture->PostProcessSettings.bOverride_MotionBlurAmount=true;Capture->PostProcessSettings.MotionBlurAmount=0;
    Capture->PostProcessSettings.bOverride_MotionBlurMax=true;Capture->PostProcessSettings.MotionBlurMax=0;
    if(!FMath::IsNearlyZero(ExposureCompensation)) {
        Capture->PostProcessSettings.bOverride_AutoExposureBias=true;
        Capture->PostProcessSettings.AutoExposureBias=View.PostProcessSettings.AutoExposureBias+FMath::Clamp(ExposureCompensation,-6.f,6.f);
        Capture->PostProcessBlendWeight=1;
    }
    Capture->CaptureScene();
    auto State=MakeShared<FBridgeGpuFrame,ESPMode::ThreadSafe>();
    State->Width=W;State->Height=H;State->Quality=FMath::Clamp(Quality,30,95);State->Sequence=++Sequence;
    State->Session=Session;State->Input=InputSequence;State->CapturedAt=Now;Readbacks.Add(State);
    State->Revision=ModeRevision;State->V3=ClientV3;State->HasMask=Mask;
    const FVector Relative=(View.Location-Anchor)/100;
    State->MCCamera=MCOrigin+FVector(-Relative.Y,Relative.Z,Relative.X);
    State->Yaw=View.Rotation.Yaw;State->Pitch=-View.Rotation.Pitch;
    State->VerticalFov=FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(View.FOV)*.5f)*float(H)/float(W)));
    // Hold the RHI texture across a resolution change. CaptureScene and copy use render queue order.
    FTextureRHIRef Texture=Target->GameThread_GetRenderTargetResource()->GetRenderTargetTexture();
    if(Share) {
        if(!SharedGpu) SharedGpu=MakeShared<FBridgeSharedTransport,ESPMode::ThreadSafe>();
        State->GPU=true;State->Transport=SharedGpu;
        ENQUEUE_RENDER_COMMAND(BridgePublishSharedFrame)([State,Texture](FRHICommandListImmediate& RHICmdList) {
            const auto& Pool=State->Transport;
            // Flush submits the RHI command queue, never waits for GPU completion. The query is polled later.
            RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
            if(!Pool->Producer) {
                Pool->Producer=MakeUnique<BridgeSharedGpu::Producer>();
                const bool D3D11=GDynamicRHI && FString(GDynamicRHI->GetName())==TEXT("D3D11");
                const bool Initialized=D3D11 && Pool->Producer->Initialize(GDynamicRHI->RHIGetNativeDevice(),State->Width,State->Height);
                Pool->Initialized.store(Initialized);Pool->Failed.store(!Initialized);
                {FScopeLock Lock(&Pool->DiagnosticLock);Pool->Diagnostic=Initialized ? TEXT("D3D11/NVIDIA shared BGRA8")
                    : D3D11 ? UTF8_TO_TCHAR(Pool->Producer->Error().c_str()) : TEXT("GPU sharing requires UE -d3d11; current RHI uses JPEG");}
                if(!Initialized) UE_LOG(LogTemp,Warning,TEXT("Bridge video: %s"),*Pool->Diagnostic);
            }
            if(!Pool->Failed.load() && Texture.IsValid() && Pool->Producer->Submit(Texture->GetNativeResource(),State->HasMask,State->Sequence))
                Pool->Pending.Add(State->Sequence,State);
            else State->Done.store(true);
            SharedFailure(Pool);
        });
        return;
    }
    ENQUEUE_RENDER_COMMAND(BridgeReadPixelsAsync)([State,Texture](FRHICommandListImmediate& RHICmdList) {
        if(!Texture.IsValid()) {State->Done.store(true);return;}
        State->Readback=MakeUnique<FRHIGPUTextureReadback>(TEXT("UEBridgeFrame"));
        State->Readback->EnqueueCopy(RHICmdList,Texture.GetReference());
    });
}
