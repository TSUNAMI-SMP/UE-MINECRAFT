#include "BridgeBlockPreview.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "BridgeBlockPalette.h"

ABridgeBlockPreview::ABridgeBlockPreview() {
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Mesh.Succeeded()) Cube = Mesh.Object;
}
void ABridgeBlockPreview::Clear() {
    for (auto& Group : Groups) if (Group) Group->DestroyComponent();
    Groups.Empty();
}
void ABridgeBlockPreview::Replace(const TArray<FBridgeBlock>& Blocks, const FVector& Anchor, UMaterialInterface* Material,UBridgeBlockPalette* Palette) {
    Clear(); if (!Cube) return;
    TMap<FString, UInstancedStaticMeshComponent*> ByMaterial;
    for (const auto& Block : Blocks) {
        UMaterialInterface* Textured=Palette ? Palette->Find(Block.BlockId) : nullptr;
        const FString Key=FString::Printf(TEXT("%s#%d"),Textured ? *Block.BlockId : TEXT(""),Block.Color);
        UInstancedStaticMeshComponent*& Group = ByMaterial.FindOrAdd(Key);
        if (!Group) {
            Group = NewObject<UInstancedStaticMeshComponent>(this);
            Group->SetMobility(EComponentMobility::Movable); Group->SetupAttachment(RootComponent);
            Group->SetStaticMesh(Cube); Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Group->SetCanEverAffectNavigation(false); Group->SetGenerateOverlapEvents(false);
            if (Textured || Material) {
                UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Textured ? Textured : Material, this);
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
