#include "BridgeRealisticExplosion.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
ABridgeRealisticExplosion::ABridgeRealisticExplosion() {
    PrimaryActorTick.bCanEverTick=true;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("ExplosionRoot"));
    Fire=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ExplosionFire"));Smoke=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ExplosionSmoke"));
    for(auto* C:{Fire.Get(),Smoke.Get()}) {C->SetupAttachment(RootComponent);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);}
    Tags.Add(TEXT("BridgeRealisticEffect"));
}
ABridgeRealisticExplosion* ABridgeRealisticExplosion::Spawn(UWorld* World,const FVector& Position,bool Realistic,int32 Quality) {
    if(!World) return nullptr;auto* Actor=World->SpawnActor<ABridgeRealisticExplosion>(Position,FRotator::ZeroRotator);if(!Actor) return nullptr;
    Actor->Count=Quality==0?12:Quality==2?48:24;
    auto Material=[&](const TCHAR* Kind){const FString Name=FString::Printf(TEXT("M_Realistic%s_%s_v1"),Kind,Realistic?TEXT("Lit"):TEXT("Simple"));auto* Base=LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Bridge/Realistic/%s.%s"),*Name,*Name));return Base?UMaterialInstanceDynamic::Create(Base,Actor):nullptr;};
    Actor->FireMaterial=Material(TEXT("Fire"));Actor->SmokeMaterial=Material(TEXT("Smoke"));
    if(Actor->FireMaterial) Actor->Fire->SetMaterial(0,Actor->FireMaterial);
    if(Actor->SmokeMaterial) Actor->Smoke->SetMaterial(0,Actor->SmokeMaterial);
    Actor->ShowAge(0);return Actor;
}
void ABridgeRealisticExplosion::Tick(float DeltaSeconds) {Super::Tick(DeltaSeconds);Age+=DeltaSeconds;if(Age>5) Destroy();else ShowAge(Age);}
void ABridgeRealisticExplosion::ShowAge(float Seconds) {
    Age=Seconds;Fire->ClearInstances();Smoke->ClearInstances();
    for(int I=0;I<Count;++I) {
        const float Angle=I*2.399963f,Z=float((I*17)%23)/23.f;
        const FVector Direction(FMath::Cos(Angle)*FMath::Sqrt(1-Z*Z),FMath::Sin(Angle)*FMath::Sqrt(1-Z*Z),Z);
        if(Seconds<.8f) Fire->AddInstance(FTransform(FQuat::Identity,Direction*Seconds*250,FVector((1+Seconds*3)*(1-Seconds/.8f))),false);
        const float Scale=(.4f+Seconds*.65f)*(1-Z*.45f);
        Smoke->AddInstance(FTransform(FQuat::Identity,Direction*Seconds*180+FVector(0,0,Seconds*180),FVector(Scale)),false);
    }
    if(SmokeMaterial) SmokeMaterial->SetScalarParameterValue(TEXT("PhysicalOpacity"),FMath::Clamp((5-Seconds)/5*.65f,0.f,.65f));
}
