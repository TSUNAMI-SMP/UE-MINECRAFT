#include "BridgeRealisticWorld.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "BridgeWorld.h"
#include "BridgeNativeUiPalette.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Misc/DefaultValueHelper.h"

namespace {
using namespace BridgeRealistic;
Vec V(FVector P) {return {P.X,P.Y,P.Z};}
FVector U(Vec P) {return FVector(P.x,P.y,P.z);}
TArray<TSharedPtr<FJsonValue>> Position(Vec P) {return {MakeShared<FJsonValueNumber>(P.x),MakeShared<FJsonValueNumber>(P.y),MakeShared<FJsonValueNumber>(P.z)};}
bool ReadVec(const TSharedPtr<FJsonObject>& O,const TCHAR* Field,Vec& Out) {
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!O->TryGetArrayField(Field,Values)||Values->Num()!=3) return false;
    double* Coords[]={&Out.x,&Out.y,&Out.z};for(int I=0;I<3;++I) if(!(*Values)[I].IsValid()||!(*Values)[I]->TryGetNumber(*Coords[I])||!std::isfinite(*Coords[I])||std::abs(*Coords[I])>1e8) return false;return true;
}
bool Integer(const TSharedPtr<FJsonObject>& O,const TCHAR* Field,int64 Min,int64 Max,int64& Out) {
    double N;if(!O->TryGetNumberField(Field,N)||!FMath::IsFinite(N)||N<Min||N>Max||N!=FMath::FloorToDouble(N)) return false;Out=int64(N);return true;
}
}
ABridgeRealisticWorld::ABridgeRealisticWorld() {
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("RealisticRoot"));
    auto Instances=[this](const TCHAR* Name,const TCHAR* MeshName,bool Collision) {
        auto* C=CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);C->SetupAttachment(RootComponent);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,MeshName));C->SetMobility(EComponentMobility::Movable);
        C->SetCollisionEnabled(Collision?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
        C->SetCollisionObjectType(ECC_WorldStatic);C->SetCollisionResponseToAllChannels(ECR_Block);
        C->SetCastShadow(false);C->SetGenerateOverlapEvents(false);return C;
    };
    SandMesh=Instances(TEXT("PhysicalSand"),TEXT("/Engine/BasicShapes/Cube.Cube"),false);
    SandSupportMesh=Instances(TEXT("SandCollision"),TEXT("/Engine/BasicShapes/Sphere.Sphere"),true);SandSupportMesh->SetHiddenInGame(true);
    TntMesh=Instances(TEXT("PhysicalTnt"),TEXT("/Engine/BasicShapes/Cube.Cube"),true);
    RockMesh=Instances(TEXT("SolidifiedFluid"),TEXT("/Engine/BasicShapes/Cube.Cube"),true);
    SplashMesh=Instances(TEXT("ConservedDroplets"),TEXT("/Engine/BasicShapes/Sphere.Sphere"),false);
    WaterMesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PhysicalWater"));WaterMesh->SetupAttachment(RootComponent);WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);WaterMesh->SetCastShadow(false);
    LavaMesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PhysicalLava"));LavaMesh->SetupAttachment(RootComponent);LavaMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);LavaMesh->SetCastShadow(false);
}
void ABridgeRealisticWorld::Initialize(ABridgeWorld* InTerrain,UBridgeNativeUiPalette* InResources) {
    Terrain=InTerrain;Resources=InResources;
    if(Terrain) {
        const FVector Corner=Terrain->BlockCenter(FIntVector::ZeroValue)-FVector(50);
        Physics.gridOrigin={FMath::Fmod(FMath::Fmod(Corner.X,25.)+25.,25.),FMath::Fmod(FMath::Fmod(Corner.Y,25.)+25.,25.),FMath::Fmod(FMath::Fmod(Corner.Z,25.)+25.,25.)};
    }
    RefreshCollisionQuery();
    Physics.inside=[this](Vec P) {return Terrain&&Terrain->ContainsUEPosition(U(P));};
    // Terrain meshes belong to cell actors, so identify imported geometry by tag.
    Physics.blocked=[this](Vec P,double Radius) {
        if(!Terrain||!Terrain->ContainsUEPosition(U(P))) return true;
        const FVector Position=U(P);const double R=Radius;
        for(int X=-1;X<=1;X+=2) for(int Y=-1;Y<=1;Y+=2) for(int Z=-1;Z<=1;Z+=2)
            if(Terrain->IsOpaqueVoxel(Terrain->SourceVoxelAt(Position+FVector(X*R,Y*R,Z*R)))) return true;
        Terrain->EnsureCollisionForPosition(Position);
        FHitResult H;
        return GetWorld()->SweepSingleByChannel(H,Position,Position+FVector(0,0,.01),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(float(Radius)),TerrainQuery);
    };
    Physics.sweep=[this](Vec A,Vec B,double Radius) {
        if(Terrain) Terrain->EnsureCollisionForPosition(U(B));
        FHitResult H;
        const bool Hit=GetWorld()->SweepSingleByChannel(H,U(A),U(B),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(float(Radius)),TerrainQuery);
        return BridgeRealistic::Hit{Hit,Hit?double(H.Time):1,V(H.ImpactNormal)};
    };
    Physics.exposure=[this](Vec A,Vec B) {
        FHitResult H;
        return GetWorld()->LineTraceSingleByChannel(H,U(A),U(B),ECC_WorldStatic,TerrainQuery)?0.:1.;
    };
    Physics.exploded=[this](const Blast& B){UndoClear.Reset();if(OnBlast) OnBlast(U(B.p),float(B.radius));};
    SetVisuals(Realistic,QualityLevel);
}
void ABridgeRealisticWorld::RefreshCollisionQuery() {
    TerrainQuery=FCollisionQueryParams(SCENE_QUERY_STAT(BridgePhysicalTerrain),false,this);
    // Pawn capsules block ECC_WorldStatic traces too. They must not be mistaken
    // for terrain at the eye position or occlude their own explosion exposure.
    if(GetWorld()) for(TActorIterator<APawn> It(GetWorld());It;++It) TerrainQuery.AddIgnoredActor(*It);
}
void ABridgeRealisticWorld::SetVisuals(bool Enabled,int32 Quality) {
    Realistic=Enabled;QualityLevel=FMath::Clamp(Quality,0,2);Materials.Reset();
    SandMesh->SetCastShadow(Enabled);TntMesh->SetCastShadow(Enabled);RockMesh->SetCastShadow(Enabled);
    const TCHAR* Names[]={TEXT("Sand"),TEXT("Tnt"),TEXT("Rock"),TEXT("Water"),TEXT("Water"),TEXT("Lava")};
    UMeshComponent* Meshes[]={SandMesh,TntMesh,RockMesh,SplashMesh,WaterMesh,LavaMesh};
    for(int I=0;I<6;++I) {
        const FString Name=FString::Printf(TEXT("M_Realistic%s_%s_v2"),Names[I],Enabled?TEXT("Lit"):TEXT("Simple"));
        auto* Base=LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Bridge/Realistic/%s.%s"),*Name,*Name));
        auto* Material=Base?UMaterialInstanceDynamic::Create(Base,this):nullptr;
        Materials.Add(Material);if(Material) Meshes[I]->SetMaterial(0,Material);
        else UE_LOG(LogTemp,Error,TEXT("Bridge realistic material missing: %s; rerun native import"),*Name);
    }
    RenderNow();
}
bool ABridgeRealisticWorld::Use(const FString& Item,const FVector& Eye,const FRotator& Aim,bool Collect,FString& Result) {
    RefreshCollisionQuery();const FVector Direction=Aim.Vector();FHitResult H;
    // Respect the first solid hit; item placement and collection cannot go through walls.
    // Solver sweeps ignore this actor; placement must hit its TNT/sand/rock too.
    FCollisionQueryParams PlacementQuery(SCENE_QUERY_STAT(BridgePhysicalPlacement),false);
    for(TActorIterator<APawn> It(GetWorld());It;++It) PlacementQuery.AddIgnoredActor(*It);
    const bool Found=GetWorld()->LineTraceSingleByChannel(H,Eye,Eye+Direction*500,ECC_WorldStatic,PlacementQuery);
    const double Limit=Found?FVector::Distance(Eye,H.ImpactPoint):500;
    if(Item==TEXT("minecraft:flint_and_steel")||Item==TEXT("minecraft:fire_charge")) {
        if(Physics.ignite(V(Eye),V(Direction),Limit+70)) {LastUsePosition=H.ImpactPoint;UndoClear.Reset();Result=TEXT("Realistic TNT ignited");return true;}return false;
    }
    if(Collect) {
        for(double D=0;D<=Limit;D+=8) {Vec P=V(Eye+Direction*D);const int Kind=FluidAt(U(P));
            if(Kind!=0&&Physics.collect(P,Kind)) {LastUsePosition=U(P);UndoClear.Reset();Result=Kind==Water?TEXT("uebridge:realistic_water_bucket"):TEXT("uebridge:realistic_lava_bucket");RenderNow();return true;}}
        return false;
    }
    if(!Found) {Result=TEXT("Aim at a solid surface");return false;}
    // Terrain's voxel conversion respects fractional exported origins and negative axes.
    FIntVector HitVoxel;FVector Face,TerrainHit;
    const bool OnTerrain=Terrain->Aim(Eye,Aim,500,HitVoxel,Face,nullptr,&TerrainHit);
    FIntVector Target;
    if(OnTerrain && FVector::Distance(Eye,TerrainHit)<=Limit+1) {
        Target=HitVoxel+FIntVector(FMath::RoundToInt(-Face.Y),FMath::RoundToInt(Face.Z),FMath::RoundToInt(Face.X));
    } else {
        const FVector N=H.ImpactNormal.GetAbs();FVector Axis=FVector::ZeroVector;
        if(N.Z>=N.X&&N.Z>=N.Y) Axis.Z=FMath::Sign(H.ImpactNormal.Z);
        else if(N.X>=N.Y) Axis.X=FMath::Sign(H.ImpactNormal.X);else Axis.Y=FMath::Sign(H.ImpactNormal.Y);
        HitVoxel=Terrain->SourceVoxelAt(H.ImpactPoint-Axis*.1f);
        Target=HitVoxel+FIntVector(FMath::RoundToInt(-Axis.Y),FMath::RoundToInt(Axis.Z),FMath::RoundToInt(Axis.X));
    }
    const FVector Point=Terrain->BlockCenter(Target);
    if(!Terrain->ContainsUEPosition(Point)) {Result=TEXT("Outside exported world");return false;}
    FString Occupant,State;if(Terrain->GetBlockState(Target,Occupant,State)) {Result=TEXT("Target block is occupied");return false;}
    if(Physics.placementOccupied(V(Point))) {Result=TEXT("Target block contains realistic physics; item retained");return false;}
    LastUsePosition=Point;
    bool Added=false;
    if(Item==TEXT("uebridge:realistic_sand")) Added=Physics.addSand(V(Point));
    else if(Item==TEXT("uebridge:realistic_tnt")) Added=Physics.addTnt(V(Point));
    else if(Item==TEXT("uebridge:realistic_water_bucket")) Added=Physics.pourBlock(V(Point),Water);
    else if(Item==TEXT("uebridge:realistic_lava_bucket")) Added=Physics.pourBlock(V(Point),Lava);
    Result=Added?TEXT("Realistic physics placed"):TEXT("No space or physics budget reached; item retained");
    if(Added) {UndoClear.Reset();RenderNow();}return Added;
}
int32 ABridgeRealisticWorld::FluidAt(const FVector& Position) const {
    auto It=Physics.liquids.find(Physics.cell(V(Position)));if(It==Physics.liquids.end()) return 0;
    const double Bottom=Physics.gridOrigin.z+It->first.z*CellSize,Height=CellSize*It->second.amount/CellCapacity;
    return Position.Z<=Bottom+Height?It->second.kind:0;
}
TArray<FVector> ABridgeRealisticWorld::CollisionAnchors() const {
    TArray<FVector> Result;TSet<FIntVector> Seen;
    auto Add=[&](Vec P) {const FVector Position=U(P);const FIntVector Cell(FMath::FloorToInt(Position.X/800),FMath::FloorToInt(Position.Y/800),FMath::FloorToInt(Position.Z/800));
        if(Result.Num()<128 && !Seen.Contains(Cell)) {Seen.Add(Cell);Result.Add(Position);}};
    for(const auto& B:Physics.bombs) Add(B.p);for(const auto& G:Physics.grains) Add(G.p);for(const auto& D:Physics.drops) Add(D.p);
    return Result;
}
void ABridgeRealisticWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);if(ReplayActive||!Terrain||!Terrain->IsSealed()) return;
    if(Physics.grains.empty()&&Physics.liquids.empty()&&Physics.bombs.empty()&&Physics.drops.empty()) {
        Accumulator=0;SolverMillis=RenderMillis=0;return; // Static rocks need no solver or collision-query rebuild.
    }
    const double SolveStart=FPlatformTime::Seconds();
    RefreshCollisionQuery();
    Accumulator+=DeltaSeconds;
    // Keep backlog rather than varying physics dt or silently dropping time.
    int Steps=0;while(Accumulator>=StepSeconds&&Steps<16) {++Steps;Physics.step();Accumulator-=StepSeconds;}
    SolverMillis=SolverMillis*.95+(FPlatformTime::Seconds()-SolveStart)*1000*.05;
    VisualClock=Physics.tick*StepSeconds;
    const bool Moving=!Physics.liquids.empty()||!Physics.drops.empty()||std::any_of(Physics.grains.begin(),Physics.grains.end(),[](const Grain& G){return !G.sleeping;})||std::any_of(Physics.bombs.begin(),Physics.bombs.end(),[](const Bomb& B){return B.fuse>=0;});
    if(Steps>0&&(Moving||HadMovingPhysics)) RenderNow();
    HadMovingPhysics=Moving;
    if(OnSound&&VisualClock>=NextFluidSound&&!Physics.liquids.empty()) {
        const auto& Fluid=*Physics.liquids.begin();
        OnSound(Fluid.second.kind==Lava?TEXT("minecraft:block.lava.ambient"):TEXT("minecraft:block.water.ambient"),U(Physics.center(Fluid.first)),.25f,1.f);
        NextFluidSound=VisualClock+1.5;
    }
    if(OnSound&&VisualClock>=NextSandSound&&std::any_of(Physics.grains.begin(),Physics.grains.end(),[](const Grain& G){return !G.sleeping&&G.v.length()>80;})) {
        OnSound(TEXT("minecraft:block.sand.step"),U(Physics.grains.front().p),.3f,.8f);NextSandSound=VisualClock+.5;
    }
}
void ABridgeRealisticWorld::BuildFluid(int32 Kind,UProceduralMeshComponent* Mesh,double Clock) {
    uint64 Signature=1469598103934665603ULL;
    for(const auto& Entry:Physics.liquids) if(Entry.second.kind==Kind) {
        for(int N:{Entry.first.x,Entry.first.y,Entry.first.z,Entry.second.amount}) {Signature^=uint32(N);Signature*=1099511628211ULL;}
    }
    const int SignatureIndex=Kind==Water?0:1;if(FluidSignatures[SignatureIndex]==Signature) return;
    FluidSignatures[SignatureIndex]=Signature;
    TArray<FVector> Points,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    auto Quad=[&](const FVector& A,const FVector& B,const FVector& C,const FVector& D,const FVector& N,float Foam) {
        const int32 Start=Points.Num();Points.Append({A,B,C,D});Normals.Append({N,N,N,N});UV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
        for(int I=0;I<4;++I) {Colors.Add(FLinearColor(Foam,0,0,1));Tangents.Add(FProcMeshTangent(1,0,0));}
        // UE reflected winding: outward normal points towards the viewer.
        Indices.Append({Start,Start+2,Start+1,Start,Start+3,Start+2});
    };
    for(const auto& Entry:Physics.liquids) {
        if(Entry.second.kind!=Kind) continue;const Key K=Entry.first;
        const double X=Physics.gridOrigin.x+K.x*CellSize,Y=Physics.gridOrigin.y+K.y*CellSize,Z=Physics.gridOrigin.z+K.z*CellSize,H=CellSize*Entry.second.amount/CellCapacity;
        const double Wave=0; // Animated normals live in the shader; topology is updated only when volume changes.
        const double Top=Z+H+Wave;
        const auto Above=Physics.liquids.find({K.x,K.y,K.z+1});
        if(Above==Physics.liquids.end()||Above->second.kind!=Kind) Quad(FVector(X,Y,Top),FVector(X+25,Y,Top),FVector(X+25,Y+25,Top),FVector(X,Y+25,Top),FVector::UpVector,Entry.second.amount<150?.04f:0);
        for(int D=0;D<4;++D) {
            Key Offset=Simulation::directions()[D];auto Other=Physics.liquids.find({K.x+Offset.x,K.y+Offset.y,K.z});
            const double Low=Other==Physics.liquids.end()?Z:Z+CellSize*Other->second.amount/CellCapacity;if(Low>=Z+H-.01) continue;
            if(D==0) Quad(FVector(X+25,Y,Low),FVector(X+25,Y+25,Low),FVector(X+25,Y+25,Top),FVector(X+25,Y,Top),FVector(1,0,0),.02f);
            if(D==1) Quad(FVector(X,Y+25,Low),FVector(X,Y,Low),FVector(X,Y,Top),FVector(X,Y+25,Top),FVector(-1,0,0),.02f);
            if(D==2) Quad(FVector(X+25,Y+25,Low),FVector(X,Y+25,Low),FVector(X,Y+25,Top),FVector(X+25,Y+25,Top),FVector(0,1,0),.02f);
            if(D==3) Quad(FVector(X,Y,Low),FVector(X+25,Y,Low),FVector(X+25,Y,Top),FVector(X,Y,Top),FVector(0,-1,0),.02f);
        }
    }
    Mesh->CreateMeshSection_LinearColor(0,Points,Indices,Normals,UV,Colors,Tangents,false);
}
void ABridgeRealisticWorld::RenderNow(double Clock) {
    const double RenderStart=FPlatformTime::Seconds();
    if(Clock<0) Clock=Physics.tick*StepSeconds;
    auto Instances=[](UInstancedStaticMeshComponent* Mesh,const TArray<FTransform>& Transforms) {
        if(Mesh->GetInstanceCount()==Transforms.Num()) {if(!Transforms.IsEmpty()) Mesh->BatchUpdateInstancesTransforms(0,Transforms,true,true,true);}
        else {Mesh->ClearInstances();Mesh->AddInstances(Transforms,false,true);}
    };
    TArray<FTransform> Sand,Support,Tnt,Rock,Splash;int32 GrainIndex=0;
    for(const auto& G:Physics.grains) {
        Support.Add(FTransform(FQuat::Identity,U(G.p),FVector(.11)));
        const int Detail=QualityLevel==0?1:8;
        for(int J=0;J<Detail;++J) {
            const uint32 Seed=uint32(GrainIndex*73856093u+J*19349663u);
            const FVector Offset=Detail==1?FVector::ZeroVector:FVector((J&1?1:-1)*2.7,(J&2?1:-1)*2.7,(J&4?1:-1)*2.7);
            const FQuat Rotation=FRotator(Seed%180,(Seed>>8)%180,(Seed>>16)%180).Quaternion();
            Sand.Add(FTransform(Rotation,U(G.p)+Offset,FVector(Detail==1?.07:.035)));
        }
        ++GrainIndex;
    }
    for(const auto& B:Physics.bombs) Tnt.Add(FTransform(FQuat::Identity,U(B.p),FVector(.98)));
    for(const auto& R:Physics.rocks) Rock.Add(FTransform(FQuat::Identity,U(Physics.center(R.first)),FVector(.25)));
    for(const auto& D:Physics.drops) Splash.Add(FTransform(FQuat::Identity,U(D.p),FVector(.04)));
    Instances(SandMesh,Sand);Instances(SandSupportMesh,Support);Instances(TntMesh,Tnt);Instances(RockMesh,Rock);Instances(SplashMesh,Splash);
    for(auto& M:Materials) if(M) {M->SetScalarParameterValue(TEXT("PhysicalClock"),float(Clock));M->SetScalarParameterValue(TEXT("PhysicalQuality"),float(QualityLevel));}
    BuildFluid(Water,WaterMesh,Clock);BuildFluid(Lava,LavaMesh,Clock);
    RenderMillis=RenderMillis*.95+(FPlatformTime::Seconds()-RenderStart)*1000*.05;
}
FString ABridgeRealisticWorld::Statistics() const {return FString::Printf(TEXT("Sand=%d fluidCells=%d TNT=%d droplets=%d water=%.3f lava=%.3f buckets solverMs=%.2f physicalMeshMs=%.2f"),int32(Physics.grains.size()),int32(Physics.liquids.size()),int32(Physics.bombs.size()),int32(Physics.drops.size()),double(Physics.volume(Water))/BucketVolume,double(Physics.volume(Lava))/BucketVolume,SolverMillis,RenderMillis);}
FString ABridgeRealisticWorld::Command(const TArray<FString>& Args,const FVector& Player) {
    if(Args.Num()==2&&Args[1]==TEXT("status")) return Statistics();
    if(Args.Num()==2&&Args[1]==TEXT("undo")) {
        if(!UndoClear.IsValid()) return TEXT("No physics deletion to undo");
        if(!ImportState(UndoClear)) return TEXT("Undo validation failed; current physics retained");UndoClear.Reset();RenderNow();return TEXT("Physics deletion restored");
    }
    if(Args.Num()>=2&&Args.Num()<=4&&Args[1]==TEXT("clear")) {
        int Kind=-1;double Radius=1600;
        if(Args.Num()>=3&&Args[2]!=TEXT("all")) {
            const FString Names[]={TEXT("sand"),TEXT("water"),TEXT("lava"),TEXT("tnt"),TEXT("rock")};Kind=-2;
            for(int I=0;I<5;++I) if(Args[2]==Names[I]) Kind=I;if(Kind<0) return TEXT("Type: sand|water|lava|tnt|rock|all");
        }
        if(Args.Num()==4) {
            if(Args[3]==TEXT("all")) Radius=-1;
            else if(!FDefaultValueHelper::ParseDouble(Args[3],Radius)||!FMath::IsFinite(Radius)||Radius<=0||Radius>512) return TEXT("Radius: 1..512 blocks, or all");
            else Radius*=100;
        }
        UndoClear=ExportState();const int Count=Physics.clear(Kind,V(Player),Radius);RenderNow();
        return FString::Printf(TEXT("Removed %d physical objects/cells; /physics undo restores the pre-clear state (until restart). Normal terrain is untouched."),Count);
    }
    return TEXT("/physics status | /physics clear [sand|water|lava|tnt|rock|all] [radiusBlocks|all] | /physics undo");
}
TSharedPtr<FJsonObject> ABridgeRealisticWorld::ExportState() const {
    auto Out=MakeShared<FJsonObject>();Out->SetNumberField(TEXT("version"),1);Out->SetNumberField(TEXT("tick"),double(Physics.tick));Out->SetNumberField(TEXT("nextId"),Physics.nextId);Out->SetNumberField(TEXT("reacted"),double(Physics.reacted));
    Out->SetBoolField(TEXT("visuals"),Realistic);Out->SetNumberField(TEXT("quality"),QualityLevel);
    auto Motion=[](Vec P,Vec Velocity){auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("p"),Position(P));O->SetArrayField(TEXT("v"),Position(Velocity));return O;};
    TArray<TSharedPtr<FJsonValue>> Grains,Fluids,Rocks,Bombs,Drops;
    for(const auto& G:Physics.grains) {auto O=Motion(G.p,G.v);O->SetBoolField(TEXT("sleep"),G.sleeping);Grains.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& F:Physics.liquids) {auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("p"),Position({double(F.first.x),double(F.first.y),double(F.first.z)}));O->SetNumberField(TEXT("kind"),F.second.kind);O->SetNumberField(TEXT("amount"),F.second.amount);Fluids.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& R:Physics.rocks) {auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("p"),Position({double(R.first.x),double(R.first.y),double(R.first.z)}));O->SetNumberField(TEXT("amount"),R.second);Rocks.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& B:Physics.bombs) {auto O=Motion(B.p,B.v);O->SetNumberField(TEXT("id"),B.id);O->SetNumberField(TEXT("fuse"),B.fuse);Bombs.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& D:Physics.drops) {auto O=Motion(D.p,D.v);O->SetNumberField(TEXT("kind"),D.kind);O->SetNumberField(TEXT("amount"),D.amount);Drops.Add(MakeShared<FJsonValueObject>(O));}
    Out->SetArrayField(TEXT("gridOrigin"),Position(Physics.gridOrigin));Out->SetArrayField(TEXT("sand"),Grains);Out->SetArrayField(TEXT("liquids"),Fluids);Out->SetArrayField(TEXT("rocks"),Rocks);Out->SetArrayField(TEXT("tnt"),Bombs);Out->SetArrayField(TEXT("drops"),Drops);return Out;
}
bool ABridgeRealisticWorld::ImportState(const TSharedPtr<FJsonObject>& State) {
    if(!State.IsValid()) return false;Simulation Next;
    const bool LegacyGrid=!State->HasField(TEXT("gridOrigin"));
    Next.gridOrigin=Physics.gridOrigin;
    if(!LegacyGrid&&(!ReadVec(State,TEXT("gridOrigin"),Next.gridOrigin)||Next.gridOrigin.x<0||Next.gridOrigin.x>=25||Next.gridOrigin.y<0||Next.gridOrigin.y>=25||Next.gridOrigin.z<0||Next.gridOrigin.z>=25)) return false;int64 N;bool Visuals;int64 Quality;
    if(!Integer(State,TEXT("version"),1,1,N)||!Integer(State,TEXT("tick"),0,9007199254740991LL,N)) return false;Next.tick=uint64(N);
    if(!Integer(State,TEXT("nextId"),1,UINT32_MAX,N)) return false;Next.nextId=uint32(N);
    if(!Integer(State,TEXT("reacted"),0,9007199254740991LL,N)) return false;Next.reacted=N;
    if(!State->TryGetBoolField(TEXT("visuals"),Visuals)||!Integer(State,TEXT("quality"),0,2,Quality)) return false;
    const TCHAR* Fields[]={TEXT("sand"),TEXT("liquids"),TEXT("rocks"),TEXT("tnt"),TEXT("drops")};const int Limits[]={8192,16384,16384,64,1024};
    for(int I=0;I<5;++I) {
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!State->TryGetArrayField(Fields[I],Rows)||Rows->Num()>Limits[I]) return false;
        for(const auto& Row:*Rows) {
            if(!Row.IsValid()||Row->Type!=EJson::Object) return false;const auto O=Row->AsObject();Vec P,Velocity;
            if(!ReadVec(O,TEXT("p"),P)) return false;
            if(I==1||I==2) {
                if(P.x!=std::floor(P.x)||P.y!=std::floor(P.y)||P.z!=std::floor(P.z)) return false;
                Key K{int(P.x),int(P.y),int(P.z)};
                // Old saves used a zero-origin grid. Rebase one-to-one onto the
                // terrain grid, retaining every cell amount and shifting <=12.5 cm.
                if(LegacyGrid) K=Next.legacyCell(K);
                if(!Physics.validPosition(Next.center(K))||!Integer(O,TEXT("amount"),1,I==1?CellCapacity:2*CellCapacity,N)) return false;
                if(I==1) {int64 Kind;if(!Integer(O,TEXT("kind"),Water,Lava,Kind)||!Next.liquids.emplace(K,Liquid{int(Kind),int(N)}).second) return false;}
                else if(!Next.rocks.emplace(K,int(N)).second) return false;
            } else {
                if(!Physics.validPosition(P)||!ReadVec(O,TEXT("v"),Velocity)||Velocity.length()>1e5) return false;
                if(I==0) {bool Sleep;if(!O->TryGetBoolField(TEXT("sleep"),Sleep)) return false;Next.grains.push_back({P,Velocity,Sleep});}
                if(I==3) {double Fuse;if(!O->TryGetNumberField(TEXT("fuse"),Fuse)||!FMath::IsFinite(Fuse)||(Fuse!=-1&&(Fuse<0||Fuse>4))||!Integer(O,TEXT("id"),1,Next.nextId-1,N)) return false;
                    for(const auto& B:Next.bombs) if(B.id==uint32(N)) return false;Next.bombs.push_back({P,Velocity,Fuse,uint32(N)});}
                if(I==4) {int64 Kind;if(!Integer(O,TEXT("kind"),Water,Lava,Kind)||!Integer(O,TEXT("amount"),1,CellCapacity,N)) return false;Next.drops.push_back({P,Velocity,int(Kind),int(N)});}
            }
        }
    }
    if(Next.rocks.size()+Next.liquids.size()>MaxCells) return false;
    for(const auto& R:Next.rocks) if(Next.liquids.count(R.first)) return false;
    Next.blocked=Physics.blocked;Next.inside=Physics.inside;Next.sweep=Physics.sweep;Next.exposure=Physics.exposure;Next.exploded=Physics.exploded;
    Physics=std::move(Next);Accumulator=0;FluidSignatures[0]=FluidSignatures[1]=0;
    if(Realistic!=Visuals||QualityLevel!=Quality) SetVisuals(Visuals,int32(Quality));else RenderNow();return true;
}
