#include "BridgeNativeExplosion.h"
#include "BridgeNativeUiPalette.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectGlobals.h"

namespace {
constexpr float SpriteDuration = .4f;
constexpr int32 MaxActiveExplosions = 32;
}

ABridgeNativeExplosion::ABridgeNativeExplosion() {
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    Tags.Add(TEXT("BridgeMinecraftVisual"));
    SpriteMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("VanillaExplosionSprite"));
    SetRootComponent(SpriteMesh);
    SpriteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SpriteMesh->SetGenerateOverlapEvents(false);
    SpriteMesh->SetCanEverAffectNavigation(false);
    SpriteMesh->SetCastShadow(false);
}

bool ABridgeNativeExplosion::Spawn(UWorld* World, const FVector& Position, UBridgeNativeUiPalette* Palette) {
    if (!World || !IsValid(Palette) || Position.ContainsNaN()) return false;
    int32 Active = 0;
    for (TActorIterator<ABridgeNativeExplosion> It(World); It; ++It) {
        if (!It->IsActorBeingDestroyed() && ++Active >= MaxActiveExplosions) return false;
    }
    UMaterialInterface* SpriteMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Bridge/Native/M_NativeExplosion_v1.M_NativeExplosion_v1"));
    if (!IsValid(SpriteMaterial)) {
        UE_LOG(LogTemp, Warning, TEXT("Native explosion unavailable: run native setup to create its local sprite material"));
        return false;
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABridgeNativeExplosion* Effect = World->SpawnActor<ABridgeNativeExplosion>(Position, FRotator::ZeroRotator, Params);
    if (!Effect || !Effect->Initialize(Palette, SpriteMaterial)) {
        if (Effect) Effect->Destroy();
        UE_LOG(LogTemp, Warning, TEXT("Native explosion unavailable: export/import particle/explosion sprite frames"));
        return false;
    }
    Effect->SetLifeSpan(SpriteDuration + .1f);
    Effect->UpdateSprite();
    return true;
}

bool ABridgeNativeExplosion::Initialize(UBridgeNativeUiPalette* Palette, UMaterialInterface* SpriteMaterial) {
    for (int32 Index = 0; Index < 16; ++Index) {
        UTexture2D* Frame = Palette->FindSprite(FString::Printf(TEXT("particle/explosion_%d"), Index));
        if (!IsValid(Frame)) break;
        Frames.Add(Frame);
    }
    if (Frames.IsEmpty()) {
        if (UTexture2D* Frame = Palette->FindSprite(TEXT("particle/explosion"))) Frames.Add(Frame);
    }
    if (Frames.IsEmpty()) return false;
    SpriteInstance = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
    if (!SpriteInstance) return false;
    const TArray<FVector> Vertices = {FVector(0,-50,-50), FVector(0,50,-50), FVector(0,50,50), FVector(0,-50,50)};
    const TArray<int32> Indices = {0,1,2,0,2,3};
    const TArray<FVector> Normals = {FVector(1,0,0),FVector(1,0,0),FVector(1,0,0),FVector(1,0,0)};
    const TArray<FVector2D> UV = {FVector2D(1,1),FVector2D(0,1),FVector2D(0,0),FVector2D(1,0)};
    const TArray<FLinearColor> Colors = {FLinearColor::White,FLinearColor::White,FLinearColor::White,FLinearColor::White};
    const TArray<FProcMeshTangent> Tangents = {FProcMeshTangent(0,1,0),FProcMeshTangent(0,1,0),FProcMeshTangent(0,1,0),FProcMeshTangent(0,1,0)};
    SpriteMesh->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, Colors, Tangents, false);
    SpriteMesh->SetMaterial(0, SpriteInstance);
    return true;
}

void ABridgeNativeExplosion::UpdateSprite() {
    if (!SpriteInstance || Frames.IsEmpty()) return;
    const int32 Frame = FMath::Clamp(FMath::FloorToInt(Age * Frames.Num() / SpriteDuration), 0, Frames.Num()-1);
    if (Frame != CurrentFrame) {
        CurrentFrame = Frame;
        SpriteInstance->SetTextureParameterValue(TEXT("ExplosionTexture"), Frames[Frame]);
    }
    const float Progress = FMath::Clamp(Age / SpriteDuration, 0.f, 1.f);
    const float Size = FMath::Lerp(2.5f, 4.5f, Progress);
    SpriteMesh->SetRelativeScale3D(FVector(1,Size,Size));
    SpriteInstance->SetVectorParameterValue(TEXT("ExplosionTint"), FLinearColor(1,1,1,FMath::Clamp((1-Progress)*4.f,0.f,1.f)));
    if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController()) {
        if (Controller->PlayerCameraManager) {
            const FVector ToCamera = Controller->PlayerCameraManager->GetCameraLocation() - GetActorLocation();
            if (!ToCamera.IsNearlyZero()) SetActorRotation(ToCamera.Rotation());
        }
    }
}

void ABridgeNativeExplosion::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    Age += FMath::Max(0.f, DeltaSeconds);
    if (Age >= SpriteDuration) { Destroy(); return; }
    UpdateSprite();
}
