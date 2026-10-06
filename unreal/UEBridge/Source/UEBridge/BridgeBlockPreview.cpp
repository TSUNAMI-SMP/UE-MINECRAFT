#include "BridgeBlockPreview.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"

ABridgeBlockPreview::ABridgeBlockPreview() {
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Mesh.Succeeded()) Cube = Mesh.Object;
}
void ABridgeBlockPreview::Clear() {
    for (auto& Group : Groups) if (Group) Group->DestroyComponent();
    Groups.Empty();
}
void ABridgeBlockPreview::Replace(const TArray<FBridgeBlock>& Blocks, const FVector& Anchor, UMaterialInterface* Material) {
    Clear(); if (!Cube) return;
    TMap<int32, UInstancedStaticMeshComponent*> ByColor;
    for (const auto& Block : Blocks) {
        UInstancedStaticMeshComponent*& Group = ByColor.FindOrAdd(Block.Color);
        if (!Group) {
            Group = NewObject<UInstancedStaticMeshComponent>(this);
            Group->SetMobility(EComponentMobility::Movable); Group->SetupAttachment(RootComponent);
            Group->SetStaticMesh(Cube); Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Group->SetCanEverAffectNavigation(false); Group->SetGenerateOverlapEvents(false);
            if (Material) {
                UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Material, this);
                Tint->SetVectorParameterValue(TEXT("BlockColor"), FLinearColor::FromSRGBColor(
                    FColor((Block.Color >> 16) & 255, (Block.Color >> 8) & 255, Block.Color & 255)));
                Group->SetMaterial(0, Tint);
            }
            Group->RegisterComponent(); Groups.Add(Group);
        }
        // UE's built-in cube has side 100 cm, matching one Minecraft block.
        Group->AddInstance(FTransform(FQuat::Identity, BridgeProtocol::ToUnreal(Block.Position, Anchor),
            FVector(Block.Size.Z, Block.Size.X, Block.Size.Y)), true);
    }
}
