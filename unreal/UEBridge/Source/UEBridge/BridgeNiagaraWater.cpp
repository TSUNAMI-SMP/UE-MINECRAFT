#include "BridgeNiagaraWater.h"
#include "BridgeWaterSourceMath.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraParameterStore.h"
#include <initializer_list>

namespace {
FName Parameter(UNiagaraSystem* System,const FNiagaraTypeDefinition& Type,std::initializer_list<const TCHAR*> Names) {
    for(const TCHAR* Name:Names) for(const auto& Variable:System->GetExposedParameters().ReadParameterVariables())
        if(Variable.GetName()==FName(Name) && Variable.GetType()==Type) return Variable.GetName();
    return NAME_None;
}
bool DomainParameters(UNiagaraSystem* System,FName& Size,FName& Resolution,FString& Error) {
    // Preserve typed lookup while accepting Niagara Fluids hose control names.
    Size=Parameter(System,FNiagaraTypeDefinition::GetVec3Def(),{TEXT("User.WorldSpaceSize"),TEXT("User.World Grid Extents"),TEXT("User.WorldGridExtents")});
    Resolution=Parameter(System,FNiagaraTypeDefinition::GetIntDef(),{TEXT("User.NumCellsMaxAxis"),TEXT("User.Num Cells Max Axis"),TEXT("User.ResolutionMaxAxis")});
    if(Size.IsNone() || Resolution.IsNone()) {
        Error=FString::Printf(TEXT("Water controls missing: %s%s; bucket retained. See Niagara parameter log"),
            Size.IsNone()?TEXT("World Grid Extents / WorldSpaceSize (Vector3) "):TEXT(""),
            Resolution.IsNone()?TEXT("Num Cells Max Axis (Integer)"):TEXT(""));
        for(const auto& Variable:System->GetExposedParameters().ReadParameterVariables())
            UE_LOG(LogTemp,Warning,TEXT("Bridge Niagara water parameter: %s vec3=%s int=%s"),*Variable.GetName().ToString(),
                Variable.GetType()==FNiagaraTypeDefinition::GetVec3Def()?TEXT("true"):TEXT("false"),
                Variable.GetType()==FNiagaraTypeDefinition::GetIntDef()?TEXT("true"):TEXT("false"));
        return false;
    }
    return true;
}
}
ABridgeNiagaraWater::ABridgeNiagaraWater() {
    PrimaryActorTick.bCanEverTick=false;
    Liquid=CreateDefaultSubobject<UNiagaraComponent>(TEXT("Liquid"));RootComponent=Liquid;
    Liquid->SetAutoActivate(false);Liquid->SetAutoDestroy(false);Liquid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Tags.Add(TEXT("BridgeNiagaraWater"));
}
UNiagaraSystem* ABridgeNiagaraWater::LoadTemplate(FString& Error) {
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Bridge/Realistic/NS_RealisticWater.NS_RealisticWater"));
    if(!System) {Error=TEXT("Niagara water asset missing. Enable Niagara Fluids and rerun import; bucket retained. See NIAGARA_WATER.md");return nullptr;}
    FName Size,Resolution;if(!DomainParameters(System,Size,Resolution,Error)) return nullptr;
    return System;
}
bool ABridgeNiagaraWater::Initialize(UNiagaraSystem* System,const FVector& Nozzle,FString& Error) {
    if(!System || Nozzle.ContainsNaN()) return false;
    FName Size,Resolution;if(!DomainParameters(System,Size,Resolution,Error)) return false;
    SourcePosition=Nozzle;
    const auto Center=BridgeWaterSource::Center({Nozzle.X,Nozzle.Y,Nozzle.Z});
    SetActorLocation(FVector(Center.x,Center.y,Center.z));Liquid->SetAsset(System);
    Liquid->SetVariableVec3(Size,FVector(BridgeWaterSource::Width,BridgeWaterSource::Width,BridgeWaterSource::Height));
    Liquid->SetVariableInt(Resolution,BridgeWaterSource::Resolution);
    Liquid->SetSystemFixedBounds(FBox(FVector(-500,-500,-400),FVector(500,500,400)));
    // Only set parameters exposed by this asset. Their graph bindings must
    // be checked in the installed engine template. Do not invent
    // overrides and misreport an unrelated emitter as a functioning hose.
    const FName Position=Parameter(System,FNiagaraTypeDefinition::GetPositionDef(),{TEXT("User.SourcePosition"),TEXT("User.SourceLocation")});
    const FName VectorPosition=Parameter(System,FNiagaraTypeDefinition::GetVec3Def(),{TEXT("User.SourcePosition"),TEXT("User.SourceLocation")});
    if(!Position.IsNone()) Liquid->SetVariablePosition(Position,Nozzle);
    else if(!VectorPosition.IsNone()) Liquid->SetVariableVec3(VectorPosition,Nozzle);
    const FName Velocity=Parameter(System,FNiagaraTypeDefinition::GetVec3Def(),{TEXT("User.SourceVelocity")});
    if(!Velocity.IsNone()) Liquid->SetVariableVec3(Velocity,FVector(0,0,-450));
    const FName Rate=Parameter(System,FNiagaraTypeDefinition::GetFloatDef(),{TEXT("User.SpawnRate"),TEXT("User.SourceSpawnRate")});
    if(!Rate.IsNone()) Liquid->SetVariableFloat(Rate,12000.f);
    UE_LOG(LogTemp,Display,TEXT("Bridge Niagara water: asset=%s nozzle=%s size=1000x1000x800 resolution=128 sizeParameter=%s resolutionParameter=%s sourcePosition=%s sourceVelocity=%s spawnRate=%s (unset controls use asset defaults)"),
        *System->GetPathName(),*Nozzle.ToString(),*Size.ToString(),*Resolution.ToString(),Position.IsNone()?*VectorPosition.ToString():*Position.ToString(),*Velocity.ToString(),*Rate.ToString());
    return true;
}
void ABridgeNiagaraWater::SetRunning(bool Enabled) {
    if(Enabled==Running) return;
    Running=Enabled;
    if(Enabled) Liquid->Activate(true);
    else Liquid->DeactivateImmediate();
}
