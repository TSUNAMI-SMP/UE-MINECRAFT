#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeBlockPalette.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeBlockModelTest,"UEBridge.Blocks.ModelVariantsMultipartAndNativeShapes",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeBlockModelTest::RunTest(const FString& Parameters) {
    auto* Palette=NewObject<UBridgeBlockPalette>();
    Palette->Models.Add(TEXT("minecraft:block/test"),TEXT(R"({"elements":[{"from":[0,0,0],"to":[16,8,16],"faces":{"north":{"texture":"minecraft:block/test","uv":[0,0,8,16],"rotation":90},"up":{"texture":"minecraft:block/test","tintindex":0}}}]})"));
    Palette->BlockstateDefinitions.Add(TEXT("minecraft:test"),TEXT(R"({"variants":{"facing=north":{"model":"minecraft:block/test"},"facing=east":{"model":"minecraft:block/test","y":90,"uvlock":true}}})"));
    Palette->StateShapes.Add(TEXT("minecraft:test"),TEXT(R"({"defaultState":"facing=north","modelOffset":[0.25,0.2],"states":{"facing=north":{"collision":[[0,0,0,1,0.5,1]],"outline":[[0,0,0,1,0.5,1]]},"facing=east":{"collision":[],"outline":[[0,0,0,1,0.5,1]]}}})"));
    TArray<FBridgeModelFace> North,East;
    TestTrue(TEXT("Unrotated state bakes"),Palette->BuildModel(TEXT("minecraft:test"),TEXT("facing=north"),North));
    TestTrue(TEXT("Rotated state bakes"),Palette->BuildModel(TEXT("minecraft:test"),TEXT("facing=east"),East));
    TestEqual(TEXT("Two actual faces, no invented cube sides"),North.Num(),2);
    if(North.Num()==2 && East.Num()==2) {
        const FBridgeModelFace* N=nullptr;const FBridgeModelFace* E=nullptr;
        for(const auto& Face:North) if(!Face.bTint) N=&Face;
        for(const auto& Face:East) if(!Face.bTint) E=&Face;
        if(TestNotNull(TEXT("North face"),N) && TestNotNull(TEXT("East face"),E)) {
            for(int32 I=0;I<4;++I) {
                TestTrue(TEXT("Default north plane at z=0"),FMath::IsNearlyZero(N->Vertices[I].Z));
                TestTrue(TEXT("90 degree model rotation faces east"),FMath::IsNearlyEqual(E->Vertices[I].X,1.0));
                TestTrue(TEXT("UV normalized from 16-unit model atlas"),N->UV[I].X>=0 && N->UV[I].X<=.5 && N->UV[I].Y>=0 && N->UV[I].Y<=1);
            }
        }
    }
    TArray<FBox> Collision,Outline;
    TestTrue(TEXT("Native half-slab collision resolves"),Palette->GetStateBoxes(TEXT("minecraft:test"),TEXT("facing=north"),Collision,Outline));
    if(TestEqual(TEXT("One native slab box"),Collision.Num(),1)) TestTrue(TEXT("Collision height is half a block"),FMath::IsNearlyEqual(Collision[0].Max.Y,.5));
    TestFalse(TEXT("Unknown state cannot silently use the default cube"),Palette->GetStateBoxes(TEXT("minecraft:test"),TEXT("facing=west"),Collision,Outline));
    Palette->BlockstateDefinitions.Add(TEXT("minecraft:multipart"),TEXT(R"({"multipart":[{"apply":{"model":"minecraft:block/test"}},{"when":{"OR":[{"north":"true"},{"east":"low|tall"}]},"apply":{"model":"minecraft:block/test","y":90}}]})"));
    TArray<FBridgeModelFace> Parts;
    TestTrue(TEXT("Multipart OR and alternatives resolve"),Palette->BuildModel(TEXT("minecraft:multipart"),TEXT("east=low,north=false"),Parts));
    TestEqual(TEXT("Both matching multipart models included"),Parts.Num(),4);
    const FVector Origin=Palette->GetModelOffset(TEXT("minecraft:test"),FIntVector::ZeroValue);
    TestTrue(TEXT("Vanilla origin plant offset"),Origin.Equals(FVector(-.25,-.2,-.25),.000001));
    TestTrue(TEXT("Plant offsets depend on x/z and not height"),Palette->GetModelOffset(TEXT("minecraft:test"),FIntVector(5,6,7)).Equals(Palette->GetModelOffset(TEXT("minecraft:test"),FIntVector(5,99,7))));
    return true;
}
#endif
