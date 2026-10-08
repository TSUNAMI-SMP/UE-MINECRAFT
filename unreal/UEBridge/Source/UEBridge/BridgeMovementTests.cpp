#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeCharacter.h"
#include "BridgeCharacterMovement.h"
#include "BridgeMovementMath.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeMovementModeTest,"UEBridge.Native.Player.FlightMomentumAndCrouchCamera",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeMovementModeTest::RunTest(const FString&) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    auto* Player=World->SpawnActor<ABridgeCharacter>();
    if(!TestNotNull(TEXT("Player"),Player)) {World->DestroyWorld(false);return false;}
    Player->SetAuthorityEnabled(true);
    auto* Movement=Cast<UBridgeCharacterMovement>(Player->GetCharacterMovement());
    if(!TestNotNull(TEXT("Native movement component"),Movement)) {World->DestroyWorld(false);return false;}
    Movement->SetMovementMode(MOVE_Falling);Movement->Velocity=FVector(250,-150,200);
    Player->ApplyFlight(true,true);
    TestTrue(TEXT("Enabling airborne flight preserves momentum"),Movement->Velocity.Equals(FVector(250,-150,200)));
    Player->ApplyFlight(true,false);
    TestTrue(TEXT("Disabling flight preserves momentum"),Movement->Velocity.Equals(FVector(250,-150,200)));
    Player->ApplyNativeAttackSlowdown();
    TestTrue(TEXT("Sprint attack applies horizontal .6 factor only"),Movement->Velocity.Equals(FVector(150,-90,200)));
    TestTrue(TEXT("Sneak retains a fixed feet plane"),Movement->bCrouchMaintainsBaseLocation);
    BridgeMovementMath::Eye Eye;
    TestTrue(TEXT("Crouch camera uses previous tick height"),FMath::IsNearlyEqual(Eye.update(127,.05),162.0));
    TestTrue(TEXT("Crouch camera interpolates at half tick"),FMath::IsNearlyEqual(Eye.update(127,.025),153.25));
    World->DestroyWorld(false);return true;
}
#endif
