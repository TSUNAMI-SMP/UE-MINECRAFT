// UE execution is required for this integration test; cloud tests use the same
// production helper via tools/test_character_math.py, without pretending UE ran.
#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeCharacterMath.h"
#include "BridgeParticleMath.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeCharacterMathTest,"UEBridge.Character.PresentationMath",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeCharacterMathTest::RunTest(const FString& Parameters) {
    using namespace BridgeCharacterMath;
    TestTrue(TEXT("Crouched eye retains vanilla height"),FMath::IsNearlyEqual(CrouchedEyeCm,127.0));
    TestTrue(TEXT("Crouched attack raises hand forwards"),AttackPitch(.35,0)>0);
    TestTrue(TEXT("Backward movement preserves torso heading"),FMath::IsNearlyZero(BodyYaw(0,0,180,432,false,false,.05)));
    TestTrue(TEXT("Right strafe separates head and torso"),BodyYaw(0,0,90,432,false,false,.05)>0);
    TestTrue(TEXT("Sprint FOV uses one MC tick half-step"),FMath::IsNearlyEqual(SprintFovMultiplier(1,true,.05),1.075));
    const auto Block=FirstPersonBlock(0,1,false);
    TestTrue(TEXT("Vanilla equipped block in camera space"),FVector(Block.Position.X,Block.Position.Y,Block.Position.Z).Equals(FVector(72,56,-52)));
    const auto Projected=ProjectFirstPersonPoint({50,20,-10},92);
    TestTrue(TEXT("Sprint world FOV keeps fixed70 hand projection"),FMath::IsNearlyEqual(Projected.Y/std::tan(92*Pi/360),20/std::tan(70*Pi/360)));
    TestTrue(TEXT("Slab dust follows outline geometry"),BridgeParticleMath::BoxCount(1,.5,1)==32);
    return true;
}
#endif
