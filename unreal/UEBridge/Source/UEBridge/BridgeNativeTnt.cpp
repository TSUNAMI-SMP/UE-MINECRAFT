#include "BridgeReceiver.h"
#include "BridgeBlockPreview.h"
#include "BridgeBlockPalette.h"
#include "BridgeWorld.h"
#include "BridgeVanillaEffects.h"
#include "Engine/World.h"

bool ABridgeReceiver::IgniteNativeTnt(const FIntVector& Block) {
    FString Id,State;if(!SyncedWorld || !SyncedWorld->GetBlockState(Block,Id,State) || Id!=TEXT("minecraft:tnt")) return false;
    const FVector Position=SyncedWorld->BlockCenter(Block);const int32 Index=NativeFuses.Num();
    if(!PrimeNativeTnt(Position)) return false;
    if(!SyncedWorld->BreakBlock(Block)) {if(NativeFuses[Index].Visual.IsValid()) NativeFuses[Index].Visual->Destroy();NativeFuses.RemoveAt(Index);return false;}
    PlayNativeSound(TEXT("minecraft:entity.tnt.primed"),Position,1,1,TEXT("block"));return true;
}
bool ABridgeReceiver::PrimeNativeTnt(const FVector& Position,float Remaining,const FVector& Velocity,bool Restoring) {
    if(!GetWorld() || NativeFuses.Num()>=64 || !TexturePalette) return false;
    FActorSpawnParameters Spawn;Spawn.Owner=this;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Visual=GetWorld()->SpawnActor<ABridgeBlockPreview>(Position,FRotator::ZeroRotator,Spawn);if(!Visual) return false;
    FBridgeBlock Block;Block.Position=FVector::ZeroVector;Block.BlockId=TEXT("minecraft:tnt");Block.StateKey=TEXT("unstable=false");Block.Color=0xffffff;Block.Role=1;
    Visual->Replace({Block},Position,PreviewMaterial,TexturePalette,false);Visual->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
    if(!Visual->HasContent()) {Visual->Destroy();return false;}
    FNativeFuse Fuse;Fuse.Position=Position;Fuse.Deadline=GetWorld()->GetTimeSeconds()+Remaining;Fuse.Visual=Visual;Fuse.Velocity=Velocity;
    if(!Restoring) {const float Angle=FMath::FRand()*2*PI;Fuse.Velocity=FVector(-FMath::Sin(Angle)*40,-FMath::Cos(Angle)*40,400);}
    NativeFuses.Add(Fuse);return true;
}
void ABridgeReceiver::TickNativeTnt(float DeltaSeconds) {
    NativeFuseClock+=FMath::Max(0.f,DeltaSeconds);int32 Steps=0;
    while(NativeFuseClock>=.05f && ++Steps<=20) {NativeFuseClock-=.05f;
        for(auto& Fuse:NativeFuses) {
            if(SyncedWorld) SyncedWorld->EnsureCollisionForPosition(Fuse.Position);
            Fuse.Velocity.Z-=80;const FVector Delta=Fuse.Velocity*.05f;FHitResult Hit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(NativeTnt),false,this);if(TargetCharacter) Query.AddIgnoredActor(TargetCharacter);
            const bool Blocked=GetWorld()->SweepSingleByChannel(Hit,Fuse.Position,Fuse.Position+Delta,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeBox(FVector(49)),Query);
            if(Blocked) {
                Fuse.Position+=Delta*FMath::Max(0.f,Hit.Time-.001f);
                if(Hit.Normal.Z>.6f && Fuse.Velocity.Z<=0) {Fuse.Velocity.X*=.7f;Fuse.Velocity.Y*=.7f;Fuse.Velocity.Z*=-.5f;}
                else Fuse.Velocity=FVector::VectorPlaneProject(Fuse.Velocity,Hit.Normal);
            } else Fuse.Position+=Delta;
            Fuse.Velocity*=.98f;
            if(VanillaEffects) VanillaEffects->SpawnSmoke(Fuse.Position+FVector(0,0,50));
        }
    }
    for(auto& Fuse:NativeFuses) if(Fuse.Visual.IsValid()) {
        const float Remaining=float(FMath::Max(0.,Fuse.Deadline-GetWorld()->GetTimeSeconds()));const int32 Ticks=FMath::CeilToInt(Remaining*20);
        Fuse.Visual->SetActorLocation(Fuse.Position);Fuse.Visual->SetNativeFlash((Ticks/5)%2==0);
        const float Growth=FMath::Pow(FMath::Clamp(1-Remaining/.5f,0.f,1.f),4.f)*.3f;Fuse.Visual->SetActorScale3D(FVector(1+Growth));
    }
}
