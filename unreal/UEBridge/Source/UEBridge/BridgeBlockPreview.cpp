#include "BridgeBlockPreview.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "BridgeBlockPalette.h"
#include "ProceduralMeshComponent.h"

ABridgeBlockPreview::ABridgeBlockPreview() {
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Mesh.Succeeded()) Cube = Mesh.Object;
}
void ABridgeBlockPreview::Clear() {
    for (auto& Group : Groups) if (Group) Group->DestroyComponent();
    for (auto& Group : ModelGroups) if (Group) Group->DestroyComponent();
    Groups.Empty();ModelGroups.Empty(); InstanceBlocks.Empty();
}
void ABridgeBlockPreview::Replace(const TArray<FBridgeBlock>& Blocks, const FVector& Anchor, UMaterialInterface* Material,UBridgeBlockPalette* Palette,bool Physics) {
    Clear(); if (!Cube) return;
    TMap<FString, UInstancedStaticMeshComponent*> ByMaterial;
    TSet<FIntVector> SeparateOutline;
    for(const auto& Block:Blocks) if(Block.Role==3) SeparateOutline.Add(Block.SourceBlock);
    struct FSection {TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;UMaterialInterface* Material=nullptr;int32 Color=0xffffff;};
    TMap<FString,FSection> Sections;
    for (const auto& Block : Blocks) {
        if(Block.Role==1) {
            TArray<FBridgeModelFace> Faces;
            if(Palette && Palette->BuildModel(Block.BlockId,Block.StateKey,Faces)) for(const auto& Face:Faces) {
                UMaterialInterface* FaceMaterial=Palette->FindFaceMaterial(Face.TextureId,Face.bTint);
                if(!FaceMaterial) continue;
                const int32 FaceColor=Face.bTint ? Block.Color : 0xffffff;
                const FString FaceKey=Face.TextureId+FString::Printf(TEXT("#%d#%d"),Face.bTint,FaceColor);
                FSection& Section=Sections.FindOrAdd(FaceKey);Section.Material=FaceMaterial;Section.Color=FaceColor;
                FVector Vertices[4];for(int32 I=0;I<4;++I) Vertices[I]=BridgeProtocol::ToUnreal(Block.Position+Face.Vertices[I]-FVector(.5),Anchor);
                const FVector MCNormal=FVector::CrossProduct(Face.Vertices[1]-Face.Vertices[0],Face.Vertices[2]-Face.Vertices[0]).GetSafeNormal();
                const FVector Normal=BridgeProtocol::ToDirection(MCNormal).GetSafeNormal();
                if(Normal.IsNearlyZero()) continue;
                const int32 Base=Section.Vertices.Num();
                for(int32 I=0;I<4;++I) {Section.Vertices.Add(Vertices[I]);Section.Normals.Add(Normal);Section.UV.Add(Face.UV[I]);Section.Colors.Add(FLinearColor::White);Section.Tangents.Add(FProcMeshTangent((Vertices[3]-Vertices[0]).GetSafeNormal(),false));}
                // Minecraft→UE reflects X, so winding must be reversed for outward faces.
                Section.Indices.Append({Base,Base+2,Base+1,Base,Base+3,Base+2});
            }
            continue;
        }
        const bool NativeCollision=Block.Role==2,NativeOutline=Block.Role==3;
        if(NativeCollision && !Physics) continue;
        UMaterialInterface* Textured=Palette ? Palette->Find(Block.BlockId) : nullptr;
        const bool CombinedOutline=NativeCollision && !SeparateOutline.Contains(Block.SourceBlock);
        // Hidden native hulls share one ISM per response profile. ResolveHit reads
        // the per-instance block metadata, so grouping never loses ID/state/owner.
        const FString Key=(NativeCollision || NativeOutline) ? FString::Printf(TEXT("proxy#%d#%d"),Block.Role,CombinedOutline)
            : FString::Printf(TEXT("%s#%d#%d#%d#%d"),Textured ? *Block.BlockId : TEXT(""),Block.Color,Physics && Block.Collision,Block.Role,CombinedOutline);
        UInstancedStaticMeshComponent*& Group = ByMaterial.FindOrAdd(Key);
        if (!Group) {
            Group = NewObject<UInstancedStaticMeshComponent>(this);
            Group->SetMobility(EComponentMobility::Movable); Group->SetupAttachment(RootComponent);
            Group->SetStaticMesh(Cube);
            if(NativeCollision || NativeOutline) {
                Group->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                Group->SetCollisionResponseToAllChannels(ECR_Ignore);
                Group->SetCollisionResponseToChannel(NativeCollision ? ECC_Pawn : ECC_Visibility,ECR_Block);
                if(CombinedOutline) Group->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
                Group->SetVisibility(false);Group->SetHiddenInGame(true);Group->SetCastShadow(false);
            } else if(Physics && Block.Collision) Group->SetCollisionProfileName(TEXT("BlockAll"));
            else Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Group->SetCanEverAffectNavigation(false); Group->SetGenerateOverlapEvents(false);
            if (!(NativeCollision || NativeOutline) && (Textured || Material)) {
                UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Textured ? Textured : Material, this);
                Tint->SetVectorParameterValue(TEXT("BlockColor"), FLinearColor::FromSRGBColor(
                    FColor((Block.Color >> 16) & 255, (Block.Color >> 8) & 255, Block.Color & 255)));
                Group->SetMaterial(0, Tint);
            }
            Group->RegisterComponent(); Groups.Add(Group);
        }
        InstanceBlocks.FindOrAdd(Group).Add(Block);
        // UE's built-in cube has side 100 cm, matching one Minecraft block.
        Group->AddInstance(FTransform(FQuat::Identity, BridgeProtocol::ToUnreal(Block.Position, Anchor),
            FVector(Block.Size.Z, Block.Size.X, Block.Size.Y)), true);
    }
    // One procedural component per cell, one section per texture/tint/color combination.
    // Native Minecraft collision and outline boxes remain separate indexed ISMs.
    if(!Sections.IsEmpty()) {
        auto* Mesh=NewObject<UProceduralMeshComponent>(this);Mesh->SetupAttachment(RootComponent);
        Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCanEverAffectNavigation(false);Mesh->SetGenerateOverlapEvents(false);Mesh->RegisterComponent();ModelGroups.Add(Mesh);
        int32 Index=0;
        for(auto& Pair:Sections) {
            auto& Section=Pair.Value;
            Mesh->CreateMeshSection_LinearColor(Index,Section.Vertices,Section.Indices,Section.Normals,Section.UV,Section.Colors,Section.Tangents,false);
            auto* Tint=UMaterialInstanceDynamic::Create(Section.Material,this);
            Tint->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(FColor((Section.Color>>16)&255,(Section.Color>>8)&255,Section.Color&255)));
            Mesh->SetMaterial(Index++,Tint);
        }
    }
}

bool ABridgeBlockPreview::ResolveHit(const UPrimitiveComponent* Component,int32 Instance,FBridgeBlock& Out) const {
    const auto* Blocks=InstanceBlocks.Find(Component);
    if(!Blocks || !Blocks->IsValidIndex(Instance)) return false;
    Out=(*Blocks)[Instance]; return true;
}
