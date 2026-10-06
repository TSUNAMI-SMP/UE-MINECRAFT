#include "BridgeArrow.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ABridgeArrow::ABridgeArrow() {
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision")); RootComponent = Collision;
    Collision->InitSphereRadius(3.f); Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); Mesh->SetupAttachment(Collision);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArrowMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (ArrowMesh.Succeeded()) Mesh->SetStaticMesh(ArrowMesh.Object);
    Mesh->SetRelativeScale3D(FVector(0.06, 0.06, 0.25)); Mesh->SetRelativeRotation(FRotator(90, 0, 0));
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->UpdatedComponent = Collision; Movement->InitialSpeed = 0; Movement->MaxSpeed = 6000.f;
    Movement->ProjectileGravityScale = 2.f; Movement->bRotationFollowsVelocity = true; Movement->bShouldBounce = false;
    Movement->OnProjectileStop.AddDynamic(this, &ABridgeArrow::Stopped); InitialLifeSpan = 6.f;
}
void ABridgeArrow::Launch(const FVector& Direction, float Pull, AActor* IgnoreActor) {
    if (IgnoreActor) Collision->IgnoreActorWhenMoving(IgnoreActor, true);
    SetActorRotation(Direction.Rotation()); Movement->Velocity = Direction.GetSafeNormal() * 6000.f * FMath::Clamp(Pull, 0.1f, 1.f);
    Movement->Activate(true);
}
void ABridgeArrow::Stopped(const FHitResult& Hit) { SetLifeSpan(2.f); }
