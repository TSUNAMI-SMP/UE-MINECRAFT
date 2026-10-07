#include "BridgeBlockPreview.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "BridgeBlockPalette.h"
#include "BridgeMeshingMath.h"
#include "BridgeLightingService.h"
#include "ProceduralMeshComponent.h"

ABridgeBlockPreview::ABridgeBlockPreview() {
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Mesh.Succeeded()) Cube = Mesh.Object;
}
void ABridgeBlockPreview::Clear() {
    for (auto& Group : Groups) if (Group) { Group->ClearInstances(); Group->UnregisterComponent(); Group->DestroyComponent(); }
    for (auto& Group : ModelGroups) if (Group) { Group->ClearAllMeshSections(); Group->UnregisterComponent(); Group->DestroyComponent(); }
    Groups.Empty();ModelGroups.Empty();InstanceBlocks.Empty();LightVertices.Empty();RenderedFaces=DrawSections=NativeProxies=0;
}
void ABridgeBlockPreview::Replace(const TArray<FBridgeBlock>& Source, const FVector& Anchor, UMaterialInterface* Material,UBridgeBlockPalette* Palette,bool Physics,
    const TFunction<bool(const FIntVector&)>& OpaqueAt,FBridgeLightingService* Lighting,bool RebuildVisual) {
    for(auto& Group:Groups) if(Group) { Group->ClearInstances(); Group->UnregisterComponent(); Group->DestroyComponent(); }
    Groups.Empty();InstanceBlocks.Empty();NativeProxies=0;
    UProceduralMeshComponent* ReusableMesh=nullptr;
    if(RebuildVisual) {
        // Reuse the component so repeated cell refreshes do not leave old RHI
        // resources pending while the UE render thread catches up. This is the
        // allocation that previously exhausted the Windows page file.
        if(!ModelGroups.IsEmpty()) ReusableMesh=ModelGroups[0].Get();
        if(ReusableMesh) ReusableMesh->ClearAllMeshSections();
        for(int32 I=1;I<ModelGroups.Num();++I) if(ModelGroups[I]) {
            ModelGroups[I]->ClearAllMeshSections(); ModelGroups[I]->UnregisterComponent(); ModelGroups[I]->DestroyComponent();
        }
        ModelGroups.Empty();if(ReusableMesh) ModelGroups.Add(ReusableMesh);
        LightVertices.Empty();RenderedFaces=DrawSections=0;
    }
    if(!Cube) return;
    // Compact cells retain one logical model per block. Exact native hulls are instantiated
    // only near the UE player; physics never depends on the far rendered surface.
    TArray<FBridgeBlock> Blocks;Blocks.Reserve(Source.Num());
    TSet<FIntVector> ExplicitHulls;for(const auto& Block:Source) if(Block.Role==2 || Block.Role==3) ExplicitHulls.Add(Block.SourceBlock);
    std::array<bool,512> FullCubes{};FIntVector GridOrigin=FIntVector::ZeroValue;FVector GridRelativeOrigin=FVector::ZeroVector;
    bool HasGrid=false;
    for(const auto& Block:Source) {
        if(Block.Role==1 && !RebuildVisual) { /* visual already retained */ }
        else if((Block.Role!=2 && Block.Role!=3) || Physics) Blocks.Add(Block);
        if(!Physics || Block.Role!=1 || ExplicitHulls.Contains(Block.SourceBlock) || !Palette) continue;
        TArray<FBox> Collision,Outline;
        if(!Palette->GetStateBoxes(Block.BlockId,Block.StateKey,Collision,Outline)) continue;
        const FVector Offset=Palette->GetModelOffset(Block.BlockId,Block.SourceBlock);
        const bool Unit=Collision.Num()==1 && Outline==Collision && Collision[0].Min.Equals(FVector::ZeroVector)
            && Collision[0].Max.Equals(FVector::OneVector) && Offset.IsNearlyZero();
        if(Unit) {
            if(!HasGrid) {GridOrigin=FIntVector(FMath::FloorToInt(Block.SourceBlock.X/8.0)*8,FMath::FloorToInt(Block.SourceBlock.Y/8.0)*8,FMath::FloorToInt(Block.SourceBlock.Z/8.0)*8);
                GridRelativeOrigin=Block.Position-FVector(.5)-FVector(Block.SourceBlock-GridOrigin);HasGrid=true;}
            const FIntVector Local=Block.SourceBlock-GridOrigin;
            if(Local.X>=0 && Local.X<8 && Local.Y>=0 && Local.Y<8 && Local.Z>=0 && Local.Z<8) {FullCubes[BridgeMeshingMath::Index(Local.X,Local.Y,Local.Z)]=true;continue;}
        }
        for(uint8 ShapeRole:{uint8(2),uint8(3)}) {
            if(ShapeRole==3 && Collision==Outline) continue;
            for(const FBox& Box:ShapeRole==2 ? Collision : Outline) {FBridgeBlock Hull=Block;Hull.Role=ShapeRole;Hull.Collision=ShapeRole==2;Hull.Size=Box.GetSize();
                Hull.Position=Block.Position-FVector(.5)+Box.GetCenter();Blocks.Add(MoveTemp(Hull));}
        }
    }
    if(HasGrid) for(const auto& Box:BridgeMeshingMath::PackSolid(FullCubes)) {
        FBridgeBlock Hull;Hull.Role=4; // Internal packed full cube grid; never accepted from a wire packet.
        Hull.Collision=true;Hull.HasSourceBlock=true;Hull.SourceBlock=GridOrigin+FIntVector(Box.x,Box.y,Box.z);
        Hull.Size=FVector(Box.sx,Box.sy,Box.sz);Hull.Position=GridRelativeOrigin+FVector(Box.x,Box.y,Box.z)+Hull.Size*.5;
        Blocks.Add(MoveTemp(Hull));
    }
    TMap<FString,UInstancedStaticMeshComponent*> ByMaterial;TSet<FIntVector> SeparateOutline;
    for(const auto& Block:Blocks) if(Block.Role==3) SeparateOutline.Add(Block.SourceBlock);
    struct FSection {TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV,UV1,UV2,UV3;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;TArray<FLightVertex> Lighting;UMaterialInterface* Material=nullptr;int32 Color=0xffffff;bool Atlas=false;};
    TMap<FString,FSection> Sections;
    for(const auto& Block:Blocks) {
        if(Block.Role==1) {
            if(!RebuildVisual) continue;
            TArray<FBridgeModelFace> Faces;
            if(Palette && Palette->BuildModel(Block.BlockId,Block.StateKey,Faces)) for(const auto& Face:Faces) {
                const bool OffsetZero=Palette->GetModelOffset(Block.BlockId,Block.SourceBlock).IsNearlyZero();
                if(OpaqueAt && BridgeMeshingMath::CullNativeFace({Face.CullOffset.X,Face.CullOffset.Y,Face.CullOffset.Z},OpaqueAt(Block.SourceBlock+Face.CullOffset),OffsetZero)) continue;
                UMaterialInterface* FaceMaterial=Palette->FindFaceMaterial(Face.TextureId,Face.bTint);if(!FaceMaterial) continue;
                const FString* Page=Palette->AtlasPages.Find(Face.TextureId);const FVector4* Rect=Palette->AtlasRects.Find(Face.TextureId);
                const auto* AtlasMaterial=Page ? Palette->AtlasMaterials.Find(*Page) : nullptr;
                const bool Atlas=Rect && AtlasMaterial && AtlasMaterial->Get();
                const int32 FaceColor=Face.bTint ? Block.Color : 0xffffff;
                const FLinearColor TintColor=FLinearColor::FromSRGBColor(FColor((FaceColor>>16)&255,(FaceColor>>8)&255,FaceColor&255));
                const FString FaceKey=Atlas ? TEXT("atlas#")+*Page : Face.TextureId+FString::Printf(TEXT("#%d#%d"),Face.bTint,FaceColor);
                FSection& Section=Sections.FindOrAdd(FaceKey);Section.Material=Atlas ? AtlasMaterial->Get() : FaceMaterial;Section.Color=FaceColor;Section.Atlas=Atlas;
                FVector Vertices[4];for(int32 I=0;I<4;++I) Vertices[I]=BridgeProtocol::ToUnreal(Block.Position+Face.Vertices[I]-FVector(.5),Anchor);
                std::array<BridgeMeshingMath::Point,4> NativeQuad;for(int32 I=0;I<4;++I) NativeQuad[I]={Face.Vertices[I].X,Face.Vertices[I].Y,Face.Vertices[I].Z};
                const auto Cross=BridgeMeshingMath::NativeNormal(NativeQuad);const FVector MCNormal=FVector(Cross[0],Cross[1],Cross[2]).GetSafeNormal();
                const FVector Normal=BridgeProtocol::ToDirection(MCNormal).GetSafeNormal();if(Normal.IsNearlyZero()) continue;
                const int32 Base=Section.Vertices.Num();
                for(int32 I=0;I<4;++I) {
                    FLinearColor Light=Lighting ? Lighting->Vertex(Block.SourceBlock,Face.Vertices[I],MCNormal) : FLinearColor(1,0,1,1);Light.A=Atlas ? TintColor.B : 1;
                    Section.Vertices.Add(Vertices[I]);Section.Normals.Add(Normal);Section.UV.Add(Face.UV[I]);Section.Colors.Add(Light);
                    Section.UV1.Add(Atlas ? FVector2D(Rect->X,Rect->Y) : FVector2D::ZeroVector);Section.UV2.Add(Atlas ? FVector2D(Rect->Z,Rect->W) : FVector2D::ZeroVector);
                    Section.UV3.Add(Atlas ? FVector2D(TintColor.R,TintColor.G) : FVector2D::ZeroVector);
                    Section.Lighting.Add({Block.SourceBlock,Face.Vertices[I],MCNormal,Light.A});
                    Section.Tangents.Add(FProcMeshTangent((Vertices[3]-Vertices[0]).GetSafeNormal(),false));
                }
                // This permutation reflects handedness: keep Minecraft outward normals and reverse winding.
                Section.Indices.Append({Base,Base+2,Base+1,Base,Base+3,Base+2});++RenderedFaces;
            }
            continue;
        }
        const bool NativeCollision=Block.Role==2 || Block.Role==4,NativeOutline=Block.Role==3;
        if((NativeCollision || NativeOutline) && !Physics) continue;
        UMaterialInterface* Textured=Palette ? Palette->Find(Block.BlockId) : nullptr;
        const bool CombinedOutline=NativeCollision && !SeparateOutline.Contains(Block.SourceBlock);
        const FString Key=(NativeCollision || NativeOutline) ? FString::Printf(TEXT("proxy#%d#%d"),Block.Role,CombinedOutline)
            : FString::Printf(TEXT("%s#%d#%d#%d#%d"),Textured ? *Block.BlockId : TEXT(""),Block.Color,Physics && Block.Collision,Block.Role,CombinedOutline);
        UInstancedStaticMeshComponent*& Group=ByMaterial.FindOrAdd(Key);
        if(!Group) {
            Group=NewObject<UInstancedStaticMeshComponent>(this);Group->SetMobility(EComponentMobility::Movable);Group->SetupAttachment(RootComponent);Group->SetStaticMesh(Cube);
            if(NativeCollision || NativeOutline) {
                Group->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Group->SetCollisionResponseToAllChannels(ECR_Ignore);
                Group->SetCollisionResponseToChannel(NativeCollision ? ECC_Pawn : ECC_Visibility,ECR_Block);
                if(NativeCollision) {Group->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);Group->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);}
                if(CombinedOutline) Group->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
                Group->SetVisibility(false);Group->SetHiddenInGame(true);Group->SetCastShadow(false);++NativeProxies;
            } else if(Physics && Block.Collision) Group->SetCollisionProfileName(TEXT("BlockAll"));else Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Group->SetCanEverAffectNavigation(false);Group->SetGenerateOverlapEvents(false);
            if(!(NativeCollision || NativeOutline) && (Textured || Material)) {
                auto* Tint=UMaterialInstanceDynamic::Create(Textured ? Textured : Material,this);
                Tint->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(FColor((Block.Color>>16)&255,(Block.Color>>8)&255,Block.Color&255)));Group->SetMaterial(0,Tint);
            }
            Group->RegisterComponent();Groups.Add(Group);
        }
        InstanceBlocks.FindOrAdd(Group).Add(Block);
        Group->AddInstance(FTransform(FQuat::Identity,BridgeProtocol::ToUnreal(Block.Position,Anchor),FVector(Block.Size.Z,Block.Size.X,Block.Size.Y)),true);
    }
    if(!Sections.IsEmpty()) {
        auto* Mesh=ReusableMesh;
        if(!Mesh) {
            Mesh=NewObject<UProceduralMeshComponent>(this);Mesh->SetupAttachment(RootComponent);Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);Mesh->SetGenerateOverlapEvents(false);Mesh->RegisterComponent();ModelGroups.Add(Mesh);
        }
        int32 Index=0;for(auto& Pair:Sections) {
            auto& Section=Pair.Value;Mesh->CreateMeshSection_LinearColor(Index,Section.Vertices,Section.Indices,Section.Normals,Section.UV,Section.UV1,Section.UV2,Section.UV3,Section.Colors,Section.Tangents,false,false);
            if(Section.Atlas) Mesh->SetMaterial(Index,Section.Material);
            else {auto* Tint=UMaterialInstanceDynamic::Create(Section.Material,this);Tint->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor::FromSRGBColor(FColor((Section.Color>>16)&255,(Section.Color>>8)&255,Section.Color&255)));Mesh->SetMaterial(Index,Tint);}
            LightVertices.Add(Index,MoveTemp(Section.Lighting));++Index;
        }
        DrawSections=Index;
    }
}
void ABridgeBlockPreview::Relight(FBridgeLightingService* Lighting) {
    if(!Lighting || ModelGroups.IsEmpty() || !ModelGroups[0]) return;
    auto* Mesh=ModelGroups[0].Get();
    for(const auto& Pair:LightVertices) {
        const FProcMeshSection* Section=Mesh->GetProcMeshSection(Pair.Key);if(!Section || Section->ProcVertexBuffer.Num()!=Pair.Value.Num()) continue;
        TArray<FVector> Vertices,Normals;TArray<FVector2D> UV,UV1,UV2,UV3;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
        for(int32 I=0;I<Pair.Value.Num();++I) {const auto& Vertex=Section->ProcVertexBuffer[I];const auto& Info=Pair.Value[I];
            FLinearColor Light=Lighting->Vertex(Info.Voxel,Info.Local,Info.Normal);Light.A=Info.TintBlue;
            Vertices.Add(Vertex.Position);Normals.Add(Vertex.Normal);UV.Add(Vertex.UV0);UV1.Add(Vertex.UV1);UV2.Add(Vertex.UV2);UV3.Add(Vertex.UV3);Tangents.Add(Vertex.Tangent);Colors.Add(Light);}
        Mesh->UpdateMeshSection_LinearColor(Pair.Key,Vertices,Normals,UV,UV1,UV2,UV3,Colors,Tangents,false);
    }
}
bool ABridgeBlockPreview::ResolveHit(const UPrimitiveComponent* Component,int32 Instance,FBridgeBlock& Out) const {
    const auto* Blocks=InstanceBlocks.Find(Component);if(!Blocks || !Blocks->IsValidIndex(Instance)) return false;Out=(*Blocks)[Instance];return true;
}
