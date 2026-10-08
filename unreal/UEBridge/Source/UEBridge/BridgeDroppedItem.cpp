#include "BridgeMeshingMath.h"
#include "BridgeDroppedItem.h"
#include "BridgeBlockPalette.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace {
constexpr float Radius=12.5f;
int32 VisualCopies(int32 Count) {return Count>=49 ? 5 : Count>=33 ? 4 : Count>=17 ? 3 : Count>=2 ? 2 : 1;}
UMaterialInstanceDynamic* Instance(UMaterialInterface* Source,UObject* Outer) {
    if(!Source) return nullptr;
    auto* Existing=Cast<UMaterialInstanceDynamic>(Source);UMaterialInterface* Parent=Source;
    while(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Parent)) {if(!Dynamic->Parent || Dynamic->Parent==Parent) return nullptr;Parent=Dynamic->Parent;}
    auto* Result=UMaterialInstanceDynamic::Create(Parent,Outer);if(Result && Existing) Result->CopyInterpParameters(Existing);return Result;
}
}
ABridgeDroppedItem::ABridgeDroppedItem() {
    PrimaryActorTick.bCanEverTick=true;
    Tags.Add(TEXT("BridgeMinecraftVisual"));
    Collision=CreateDefaultSubobject<USphereComponent>(TEXT("ItemCollision"));SetRootComponent(Collision);
    Collision->InitSphereRadius(Radius);Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionObjectType(ECC_WorldDynamic);Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);Collision->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    Collision->SetGenerateOverlapEvents(false);Collision->SetCanEverAffectNavigation(false);
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("NativeGroundItem"));Mesh->SetupAttachment(Collision);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);Mesh->SetGenerateOverlapEvents(false);
}
bool ABridgeDroppedItem::Initialize(UBridgeBlockPalette* Resources,const FString& Model,int32 Quantity,const FVector& InitialVelocity) {
    Palette=Resources;ModelKey=Model;Count=Quantity;Copies=VisualCopies(Count);Velocity=InitialVelocity;
    Phase=float(GetTypeHash(ModelKey)%6283)/1000.f;
    return Palette && Count>0 && Count<=99 && BuildMesh();
}
void ABridgeDroppedItem::SetQuantity(int32 Quantity) {
    Count=FMath::Clamp(Quantity,0,99);const int32 NewCopies=VisualCopies(Count);
    if(NewCopies!=Copies) {Copies=NewCopies;BuildMesh();}
}
bool ABridgeDroppedItem::BuildMesh() {
    TArray<FBridgeModelFace> Faces;
    if(!Palette || !Palette->BuildItem(ModelKey,TEXT("ground"),Faces) || Faces.IsEmpty()) return false;
    struct FSection {
        TArray<FVector> Positions,Normals;TArray<int32> Indices;TArray<FVector2D> UV;
        TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
        UMaterialInterface* Material=nullptr;FColor Tint=FColor::White;
    };
    TArray<FSection> Sections;TMap<FString,int32> Groups;FBox Bounds(ForceInit);
    for(int32 Copy=0;Copy<Copies;++Copy) for(const auto& Face:Faces) {
        const FString Group=Face.TextureId+FString::Printf(TEXT("#%u"),Face.Color.ToPackedARGB());int32 Index;
        if(const int32* Existing=Groups.Find(Group)) Index=*Existing;
        else {
            Index=Sections.Num();Groups.Add(Group,Index);auto& Section=Sections.AddDefaulted_GetRef();
            const auto* Material=Palette->ItemMaterials.Find(Face.TextureId);Section.Material=Material ? Material->Get() : nullptr;Section.Tint=Face.Color;
            if(!Section.Material) return false;
        }
        auto& Section=Sections[Index];const int32 First=Section.Positions.Num();
        const FVector Offset=Copy==0 ? FVector::ZeroVector : FVector(FMath::Sin(Phase+Copy*2.3f)*6,FMath::Cos(Phase+Copy*1.7f)*6,Copy*1.5f);
        for(int32 V=0;V<4;++V) {
            const FVector MC=Face.Vertices[V]+Face.RenderOffset;const FVector Point=FVector(MC.Z,-MC.X,MC.Y)*100.f+Offset;
            Section.Positions.Add(Point);Bounds+=Point;Section.UV.Add(Face.UV[V]);Section.Colors.Add(FLinearColor::White);
        }
        // Keep transformed MC order for UE clockwise fronts; normals remain outward.
        const FVector Normal=Face.HasNativeNormal ? FVector(Face.NativeNormal.Z,-Face.NativeNormal.X,Face.NativeNormal.Y)
            : FVector::CrossProduct(Section.Positions[First+2]-Section.Positions[First],Section.Positions[First+1]-Section.Positions[First]).GetSafeNormal();
        const FVector Tangent=(Section.Positions[First+1]-Section.Positions[First]).GetSafeNormal();
        for(int32 V=0;V<4;++V) {Section.Normals.Add(Normal);Section.Tangents.Add(FProcMeshTangent(Tangent,false));}
        std::array<BridgeMeshingMath::Point,4> Q;for(int32 I=0;I<4;++I) {const auto& V=Section.Positions[First+I];Q[I]={V.X,V.Y,V.Z};}
        const auto Triangles=BridgeMeshingMath::UEFacingQuad(Q,{Normal.X,Normal.Y,Normal.Z},First);
        for(int32 TriangleIndex:Triangles) Section.Indices.Add(TriangleIndex);
        if(Face.DoubleSided) for(int32 I=0;I<6;I+=3) {Section.Indices.Add(Triangles[I]);Section.Indices.Add(Triangles[I+2]);Section.Indices.Add(Triangles[I+1]);}
    }
    Mesh->ClearAllMeshSections();VisualOffset=-Bounds.Min.Z-Radius+7.f;
    for(int32 Index=0;Index<Sections.Num();++Index) {
        auto& Section=Sections[Index];Mesh->CreateMeshSection_LinearColor(Index,Section.Positions,Section.Indices,Section.Normals,Section.UV,Section.Colors,Section.Tangents,false);
        if(auto* Material=Instance(Section.Material,this)) {
            Material->SetVectorParameterValue(TEXT("BlockColor"),FLinearColor(Section.Tint.R/255.f,Section.Tint.G/255.f,Section.Tint.B/255.f,1));
            Material->SetScalarParameterValue(TEXT("BridgeUseVertexLight"),0);Material->SetVectorParameterValue(TEXT("BridgeLight"),FLinearColor(1,0,1,1));Mesh->SetMaterial(Index,Material);
        }
    }
    return true;
}
void ABridgeDroppedItem::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);if(!Active) return;
    const float Delta=FMath::Clamp(DeltaSeconds,0.f,.1f);Age+=Delta;
    const int32 Steps=FMath::Clamp(FMath::CeilToInt(Delta/.0125f),1,8);const float Step=Delta/Steps;
    for(int32 I=0;I<Steps;++I) {
        Velocity.Z-=1600.f*Step;FHitResult Hit;
        if(Contains && !Contains(GetActorLocation()+Velocity*Step)) {Velocity*= -.2f;continue;}
        AddActorWorldOffset(Velocity*Step,true,&Hit);
        if(Hit.bBlockingHit) {
            const float Along=FVector::DotProduct(Velocity,Hit.Normal);
            if(Along<0) Velocity-=Hit.Normal*Along*(Hit.Normal.Z>.6f ? 1.25f : 1.35f);
            if(Hit.Normal.Z>.6f) {Velocity.X*=FMath::Pow(.588f,Step*20.f);Velocity.Y*=FMath::Pow(.588f,Step*20.f);if(FMath::Abs(Velocity.Z)<15.f) Velocity.Z=0;}
            AddActorWorldOffset(Hit.Normal*.05f,false);
        }
        Velocity*=FMath::Pow(.98f,Step*20.f);
    }
    Mesh->SetRelativeLocation(FVector(0,0,VisualOffset+FMath::Sin(Age*2.f+Phase)*5.f));
    Mesh->SetRelativeRotation(FRotator(0,FMath::RadiansToDegrees(Age)+Phase*57.2958f,0));
}
