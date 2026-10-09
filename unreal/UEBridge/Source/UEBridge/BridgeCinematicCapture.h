#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BridgeRealisticPhysics.h"
#include "BridgeMjpegAvi.h"
#include "BridgeCinematicCapture.generated.h"

/** Disk-streamed 20 Hz scene observations, interpolated/re-rendered at 60 Hz.
 * Replay runs in a separate game process from an immutable baseline, never
 * saving over the player's world. MJPEG AVI, silent; no GPU/FPS guarantees.
 */
UCLASS()
class UEBRIDGE_API UBridgeCinematicCapture : public UActorComponent {
    GENERATED_BODY()
public:
    UBridgeCinematicCapture();
    virtual ~UBridgeCinematicCapture() override;
    virtual void TickComponent(float DeltaSeconds,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    FString ConfigureReplayFromCommandLine();
    void Initialize(class ABridgeReceiver* Receiver,class ABridgeRealisticWorld* Physics,class ABridgeWorld* Terrain,const FString& SaveFile,const FString& Package);
    FString Command(const TArray<FString>& Args);
    bool IsOfflineRender() const {return Offline;}
    bool IsRecording() const {return Recording;}
    bool HasReplayError() const {return Failed;}
    const FString& GetStatus() const {return Status;}
    void RecordExplosion(const FVector& Position,bool Realistic,int32 Quality);
private:
    UPROPERTY() TObjectPtr<class ABridgeReceiver> Receiver;
    UPROPERTY() TObjectPtr<class ABridgeRealisticWorld> Physical;
    UPROPERTY() TObjectPtr<class ABridgeWorld> Terrain;
    UPROPERTY() TMap<FString,TObjectPtr<AActor>> ReplayActors;
    UPROPERTY() TArray<TObjectPtr<class ABridgeRealisticExplosion>> ReplayEffects;
    FString Status=TEXT("/record start | /record stop | /record status"),Directory,SaveFile,Package,ExpectedPackage;
    bool Recording=false,Offline=false,Failed=false,Prepared=false,FixedTimeSaved=false;
    bool PreviousFixedTime=false;double PreviousFixedDelta=0,Elapsed=0,RecordClock=0,LastTime=-1,Duration=0,LastWrittenTime=0;
    int32 FrameCount=0,ExpectedFrames=0,LoadedFrames=0,RenderFrame=0,TotalRenderFrames=0,Width=0,Height=0;
    int64 WrittenBytes=0;
    FDelegateHandle EndFrameHandle;
    TUniquePtr<class IFileHandle> Stream,VideoFile;
    BridgeAvi::Writer Avi;
    TSharedPtr<class FJsonObject> Current,Next;
    BridgeRealistic::Simulation CurrentPhysics,NextPhysics;
    TSet<FIntVector> DirtyCells;
    TArray<TSharedPtr<class FJsonValue>> PendingExplosions;
    bool WriteObservation();
    bool ReadObservation(TSharedPtr<FJsonObject>& Out);
    bool ActivateCurrent();
    void RenderPose(double Time);
    void CaptureEndFrame();
    void FinishRender(bool Success);
    bool StopRecording();
    void Fail(const FString& Reason);
};
