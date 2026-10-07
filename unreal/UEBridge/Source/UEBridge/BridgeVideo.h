#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Async/Future.h"
#include "BridgeVideoCadence.h"
#include "BridgeVideo.generated.h"

/** Loopback video; optional leased D3D11 GPU sharing, with JPEG compatibility fallback. */
UCLASS(ClassGroup=(Bridge), meta=(BlueprintSpawnableComponent))
class UEBRIDGE_API UBridgeVideo : public UActorComponent {
    GENERATED_BODY()
public:
    UBridgeVideo();
    void Start(int32 Port);
    void SetSource(class UCameraComponent* Camera,const FString& Session,uint64 InputSequence);
    void SetRenderMode(bool Lighting,bool VanillaSky);
    void SetMinecraftOrigin(const FVector& MinecraftOrigin,const FVector& UEAnchor) {MCOrigin=MinecraftOrigin;Anchor=UEAnchor;}
    bool IsLightingEnabled() const {return LightingEnabled;}
    bool IsVanillaSkyEnabled() const {return VanillaSkyEnabled && ClientV3;}
    int32 GetMaskPixels() const {return LastMaskPixels;}
    int32 GetMaskForegroundPixels() const {return LastMaskForeground;}
    int32 GetMaskTranslucentPixels() const {return LastMaskTranslucent;}
    bool AreMaskCountsAvailable() const {return LastMaskForeground>=0 && LastMaskTranslucent>=0;}
    FString GetTransportName() const;
    FString GetGpuDiagnostic() const;
    float GetCaptureMs() const {return LastCaptureMs;}
    int32 GetDroppedFrames() const {return DroppedFrames;}
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="160",ClampMax="1920")) int32 Width=960;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="90",ClampMax="1080")) int32 Height=540;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="1",ClampMax="60")) int32 FramesPerSecond=60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="30",ClampMax="95")) int32 Quality=85;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="-6",ClampMax="6")) float ExposureCompensation=0;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Video") bool Streaming=false;
private:
    void TickStream(class UCameraComponent* Camera,const FString& Session,uint64 InputSequence);
    TWeakObjectPtr<class UCameraComponent> SourceCamera;
    FString SourceSession;
    uint64 SourceInput=0;
    TArray<TSharedPtr<struct FBridgeGpuFrame,ESPMode::ThreadSafe>> Readbacks;
    class FSocket* Listener=nullptr;
    class FSocket* Client=nullptr;
    UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> Target;
    FVector MCOrigin=FVector::ZeroVector,Anchor=FVector::ZeroVector;
    bool LightingEnabled=true,VanillaSkyEnabled=false,ClientV3=false,ClientGpu=false;
    TSharedPtr<struct FBridgeSharedTransport,ESPMode::ThreadSafe> SharedGpu;
    TArray<uint8> Acknowledgements;
    float LastCaptureMs=0;
    int32 DroppedFrames=0;
    TWeakObjectPtr<class UGameViewportClient> StandaloneViewport;
    bool SavedViewportDisabled=false;
    uint32 ModeRevision=0,EncodeRevision=0;
    double LastSkyScan=-1;
    int32 LastMaskPixels=0,LastMaskForeground=0,LastMaskTranslucent=0;
    TFuture<TArray<uint8>> Encoding;
    FString EncodeSession, ClientSession;
    TArray<uint8> Hello, Output;
    int32 Sent=0;
    uint32 Sequence=0;
    double AcceptedAt=0, LastProgress=0;
    BridgeVideoCadence::Scheduler CaptureSchedule;
    void DropClient();
    void Flush();
    void ConfigureCapture(class USceneCaptureComponent2D* Component,bool Mask);
    void RefreshHiddenSky();
    void ResetSharedGpu();
    void ReadGpuAcknowledgements();
    void UpdateStandaloneViewport(bool CaptureOnly);
};
