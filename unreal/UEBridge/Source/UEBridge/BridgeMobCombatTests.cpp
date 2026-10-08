#if WITH_DEV_AUTOMATION_TESTS
#include "BridgeMobCharacter.h"
#include "BridgeMobWorld.h"
#include "BridgeCharacter.h"
#include "BridgeCharacterMovement.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeMobCombatTest,"UEBridge.Native.Mobs.HurtWindowAndGroundedDeath",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeMobCombatTest::RunTest(const FString&) {
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    auto* Mob=World->SpawnActor<ABridgeMobCharacter>();
    auto* Owner=World->SpawnActor<ABridgeMobWorld>();
    FBridgeMobAppearance Appearance;Appearance.Material=UMaterial::GetDefaultMaterial(MD_Surface);
    FBridgeMobPart Part;Part.Name=TEXT("body");Part.Parent=-1;
    // A vertical quad tests the visual bounds independently of capsule height.
    Part.Vertices={FVector(0,0,0),FVector(1,0,0),FVector(1,0,20),FVector(0,0,20)};
    Part.Texcoords={FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)};
    Appearance.Parts.Add(Part);
    FBridgeMobSnapshot Snapshot;Snapshot.Id=TEXT("test");Snapshot.Type=TEXT("minecraft:zombie");
    bool Ready=Mob && Mob->Initialize(Snapshot,Appearance,Owner);
    TestTrue(TEXT("Native mob geometry initialized"),Ready);
    if(Ready) {
        Mob->SetActorLocation(FVector(0,0,90));Mob->SetAuthority(true,nullptr);
        TestTrue(TEXT("First attack accepted"),Mob->Hit(4,FVector(1,0,0)));
        TestFalse(TEXT("Equal hit rejected in first 10 ticks"),Mob->Hit(4,FVector(1,0,0)));
        TestFalse(TEXT("Weaker hit rejected"),Mob->Hit(2,FVector(1,0,0)));
        TestTrue(TEXT("Stronger attack applies excess"),Mob->Hit(7,FVector(1,0,0)));
        TestEqual(TEXT("20 - 4 - (7-4) health"),Mob->NativeSnapshot(FVector::ZeroVector,FVector::ZeroVector).Health,13.f);
        TestTrue(TEXT("Fatal stronger hit accepted"),Mob->Hit(100,FVector(1,0,0)));
        TestFalse(TEXT("Dead mob is not alive"),Mob->Alive());Mob->Tick(.1f);
        float Bottom=TNumericLimits<float>::Max();TInlineComponentArray<UProceduralMeshComponent*> Parts;Mob->GetComponents(Parts);
        for(auto* Mesh:Parts) if(Mesh->GetNumSections()>0) Bottom=FMath::Min(Bottom,float(Mesh->CalcBounds(Mesh->GetComponentTransform()).GetBox().Min.Z));
        TestTrue(TEXT("Fallen model bottom rests at feet plane"),FMath::IsNearlyZero(Bottom,.1f));
    }
    const auto* Character=GetDefault<ABridgeCharacter>();
    TestNotNull(TEXT("Creative flight uses the custom movement component"),Cast<UBridgeCharacterMovement>(Character->GetCharacterMovement()));
    World->DestroyWorld(false);return Ready;
}
#endif
