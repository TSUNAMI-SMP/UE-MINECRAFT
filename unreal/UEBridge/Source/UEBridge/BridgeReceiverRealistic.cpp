#include "BridgeReceiver.h"
#include "BridgeRealisticWorld.h"
#include "BridgeRealisticExplosion.h"
#include "BridgeCinematicCapture.h"
#include "BridgeMobCharacter.h"
#include "BridgeMobWorld.h"
#include "BridgeWorld.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

bool ABridgeReceiver::IsNativeReplay() const {return CinematicCapture&&CinematicCapture->IsOfflineRender();}
bool ABridgeReceiver::NativeRealisticVisuals() const {return RealisticWorld&&RealisticWorld->VisualsEnabled();}
int32 ABridgeReceiver::NativeRealisticQuality() const {return RealisticWorld?RealisticWorld->GetQuality():1;}
bool ABridgeReceiver::NativeRecording() const {return CinematicCapture&&CinematicCapture->IsRecording();}
void ABridgeReceiver::NativeRealisticBlast(const FVector& Position,float Radius) {
    if(!RealisticWorld||!SyncedWorld) return;
    // Evaluate occlusion before deleting terrain. Five body samples avoid a
    // single blocked toe suppressing all blast pressure on a partially exposed mob.
    auto Exposure=[&](AActor* Actor) {
        FVector Center,Extent;Actor->GetActorBounds(true,Center,Extent);double Visible=0;
        for(const FVector& Offset:{FVector::ZeroVector,FVector(Extent.X*.7,0,0),FVector(-Extent.X*.7,0,0),FVector(0,0,Extent.Z*.7),FVector(0,0,-Extent.Z*.7)}) {
            const FVector End=Center+Offset;Visible+=RealisticWorld->Physics.exposure({Position.X,Position.Y,Position.Z},{End.X,End.Y,End.Z});
        }
        return float(Visible/5);
    };
    for(TActorIterator<ABridgeMobCharacter> It(GetWorld());It;++It) if(It->Alive()) {
        FVector Away=It->GetActorLocation()-Position;const float Impact=FMath::Max(0.f,1-Away.Size()/Radius)*Exposure(*It);if(Impact<=0) continue;
        It->Hit(FMath::FloorToFloat((Impact*Impact+Impact)*.5f*7*(Radius/100)+1),Away.GetSafeNormal());
        It->GetCharacterMovement()->Velocity+=Away.GetSafeNormal()*Impact*1800;
    }
    if(TargetCharacter&&MobWorld&&!NativeCreative) {
        FVector Away=TargetCharacter->GetActorLocation()-Position;const float Impact=FMath::Max(0.f,1-Away.Size()/Radius)*Exposure(TargetCharacter);
        if(Impact>0) {MobWorld->HitPlayer(FMath::FloorToFloat((Impact*Impact+Impact)*.5f*7*(Radius/100)+1),Position,false);TargetCharacter->GetCharacterMovement()->Velocity+=Away.GetSafeNormal()*Impact*1800;}
    }
    SyncedWorld->RemoveBlocksInSphere(Position,Radius*.5f);
    ABridgeRealisticExplosion::Spawn(GetWorld(),Position,RealisticWorld->VisualsEnabled(),RealisticWorld->GetQuality());
    if(CinematicCapture) CinematicCapture->RecordExplosion(Position,RealisticWorld->VisualsEnabled(),RealisticWorld->GetQuality());
    PlayNativeSound(TEXT("minecraft:entity.generic.explode"),Position,4,1,TEXT("block"));
}
