#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Async/Future.h"
#include "BridgeVideo.generated.h"

/** Loopback JPEG stream, independent from the input/event UDP socket. */
UCLASS(ClassGroup=(Bridge), meta=(BlueprintSpawnableComponent))
class UEBRIDGE_API UBridgeVideo : public UActorComponent {
    GENERATED_BODY()
public:
    UBridgeVideo();
    void Start(int32 Port);
    void TickStream(class UCameraComponent* Camera,const FString& Session);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="160",ClampMax="1920")) int32 Width=480;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="90",ClampMax="1080")) int32 Height=270;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="1",ClampMax="30")) int32 FramesPerSecond=15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bridge|Video",meta=(ClampMin="30",ClampMax="95")) int32 Quality=75;
    UPROPERTY(BlueprintReadOnly, Category="Bridge|Video") bool Streaming=false;
private:
    class FSocket* Listener=nullptr;
    class FSocket* Client=nullptr;
    UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> Target;
    TFuture<TArray<uint8>> Encoding;
    FString EncodeSession, ClientSession;
    TArray<uint8> Hello, Output;
    int32 Sent=0;
    uint32 Sequence=0;
    double AcceptedAt=0, LastCapture=-1, LastProgress=0;
    void DropClient();
    void Flush();
};
