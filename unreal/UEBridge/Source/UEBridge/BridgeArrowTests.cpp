#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeArrow.h"
#include "BridgeMobCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeNativeArrowHitTest,"UEBridge.Native.Arrow.ImpactDamagesMobOnce",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeNativeArrowHitTest::RunTest(const FString&) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Mob=World->SpawnActor<ABridgeMobCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    auto* Arrow=World->SpawnActor<ABridgeArrow>(FVector(0,0,300),FRotator::ZeroRotator,Spawn);
    if(!TestNotNull(TEXT("Mob"),Mob) || !TestNotNull(TEXT("Arrow"),Arrow)) {World->DestroyWorld(false);return false;}
    Mob->SetAuthority(true,nullptr);
    int32 Hits=0;
    Arrow->NativeImpact=[&Hits](AActor* Actor,const FVector& Direction,float Damage) {
        ++Hits;if(auto* Target=Cast<ABridgeMobCharacter>(Actor)) Target->Hit(Damage,Direction);
    };
    Arrow->Launch(FVector(1,0,0),1.f,nullptr);
    const FHitResult Hit(Mob,Mob->GetCapsuleComponent(),FVector::ZeroVector,FVector(-1,0,0));
    // Broadcast the production movement delegate, whose velocity can already be zero.
    Arrow->Movement->Velocity=FVector::ZeroVector;
    Arrow->Movement->OnProjectileStop.Broadcast(Hit);
    TestEqual(TEXT("Full unenchanted basic shot damages the mob"),Mob->Health,14.f);
    Arrow->Movement->OnProjectileStop.Broadcast(Hit);
    TestEqual(TEXT("Repeated stop never damages the same mob twice"),Mob->Health,14.f);
    TestEqual(TEXT("Impact callback delivered once"),Hits,1);
    auto* Visual=World->SpawnActor<ABridgeArrow>(FVector(0,0,500),FRotator::ZeroRotator,Spawn);
    if(TestNotNull(TEXT("Legacy visual arrow"),Visual)) {
        Visual->Launch(FVector(1,0,0),1.f,nullptr);Visual->Movement->OnProjectileStop.Broadcast(Hit);
        TestEqual(TEXT("Unbound bridge visual does not damage a copied mob"),Mob->Health,14.f);
    }
    World->DestroyWorld(false);return true;
}
#endif
