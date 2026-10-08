#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "BridgeLightingService.h"
#include "Engine/World.h"
namespace {
int32 KindOf(const FString& Id) {return Id==TEXT("minecraft:lava") ? 2 : Id==TEXT("minecraft:water") || Id==TEXT("minecraft:bubble_column") ? 1 : 0;}
int32 LevelOf(const FString& State) {const int32 Start=State.Find(TEXT("level="));return Start<0 ? 0 : FMath::Clamp(FCString::Atoi(*State.Mid(Start+6)),0,15);}
double HeightOf(int32 Level) {return (8-(Level>=8 ? 0 : Level))/9.;}
}
void ABridgeWorld::EnableNativeFluids(bool UltraWarm) {
    FluidUltraWarm=UltraWarm;
    if(NativeFluidsEnabled) return;NativeFluidsEnabled=true;
    for(const auto& Cell:Stored) for(const auto& Block:Cell.Value) if(Block.Role<=1 && KindOf(Block.BlockId)) QueueFluid(OwnerOf(Block));
}
void ABridgeWorld::QueueFluid(const FIntVector& Position) {
    static const FIntVector Offsets[]={FIntVector(0,0,0),FIntVector(1,0,0),FIntVector(-1,0,0),FIntVector(0,1,0),FIntVector(0,-1,0),FIntVector(0,0,1),FIntVector(0,0,-1)};
    for(const auto& Offset:Offsets) {const auto P=Position+Offset;if(!Inside(CellOf(P)) || !Stored.Contains(CellOf(P)) || FluidUpdates.Num()>=65536) continue;
        FString Id,State;GetBlockState(P,Id,State);const double Due=FluidClock+(KindOf(Id)==2 ? (FluidUltraWarm ? .5 : 1.5) : .25);
        if(auto* Existing=FluidUpdates.Find(P)) *Existing=FMath::Min(*Existing,Due);else FluidUpdates.Add(P,Due);}
}
int32 ABridgeWorld::FluidAt(const FVector& Position,FVector* Flow) const {
    if(Flow) *Flow=FVector::ZeroVector;
    const FIntVector P=SourceVoxelAt(Position);FString Id,State;const bool Have=GetBlockState(P,Id,State);
    const int32 Kind=Have ? KindOf(Id) : 0;
    const bool Waterlogged=Have && State.Contains(TEXT("waterlogged=true"));if(!Kind && !Waterlogged) return 0;
    const int32 Level=Waterlogged ? 0 : LevelOf(State);const double Surface=BlockCenter(P).Z-50+HeightOf(Level)*100;
    FString Above,AboveState;const bool Column=GetBlockState(P+FIntVector(0,1,0),Above,AboveState) && (KindOf(Above)==(Kind ? Kind : 1) || AboveState.Contains(TEXT("waterlogged=true")));
    if(!Column && Position.Z>=Surface) return 0;
    if(Flow && Kind) {
        FVector MC=FVector::ZeroVector;
        for(const FIntVector Direction:{FIntVector(1,0,0),FIntVector(-1,0,0),FIntVector(0,0,1),FIntVector(0,0,-1)}) {
            FString Other,OtherState;const bool Occupied=GetBlockState(P+Direction,Other,OtherState);
            if(!Occupied || KindOf(Other)==Kind) {const double OtherHeight=Occupied ? HeightOf(LevelOf(OtherState)) : 0;
                MC+=FVector(Direction)*(HeightOf(Level)-OtherHeight);}
        }
        *Flow=FVector(MC.Z,-MC.X,Level>=8 ? -6. : 0).GetSafeNormal()*28.;
    }
    return Kind ? Kind : 1;
}
bool ABridgeWorld::SetFluid(const FIntVector& Position,int32 Kind,int32 Level) {
    if((FluidUltraWarm && Kind==1) || !Sealed || Kind<0 || Kind>2 || Level<0 || Level>15 || !Inside(CellOf(Position))) return false;
    auto* Rows=Stored.Find(CellOf(Position));if(!Rows) return false;
    FString Existing,State;GetBlockState(Position,Existing,State);
    if(!Existing.IsEmpty() && !KindOf(Existing)) {
        if(Kind==2 || !State.Contains(TEXT("waterlogged="))) return false;
        const bool Wet=State.Contains(TEXT("waterlogged=true"));if(Wet==(Kind==1)) return true;
        FString Updated=State;Updated.ReplaceInline(Wet ? TEXT("waterlogged=true") : TEXT("waterlogged=false"),Kind==1 ? TEXT("waterlogged=true") : TEXT("waterlogged=false"));
        const auto* Original=FindVisual(Position);TArray<FBridgeBlock> NewRows;
        if(!Original || !AppendState(Position,Existing,Original->Color,Updated,NewRows)) return false;
        const int32 Before=Rows->CountByPredicate([&](const FBridgeBlock& B){return OwnerOf(B)==Position;});
        if(Rows->Num()-Before+NewRows.Num()>8192 || Shapes-Before+NewRows.Num()>4194304) return false;
        const int32 Removed=Rows->RemoveAll([&](const FBridgeBlock& B){return OwnerOf(B)==Position;});Rows->Append(NewRows);Shapes+=NewRows.Num()-Removed;
        const FIntVector Local=Position-CellOf(Position)*8;const uint16 Index=uint16(Local.X+Local.Z*8+Local.Y*64);auto& Water=WaterCells.FindOrAdd(CellOf(Position));if(Kind==1) Water.AddUnique(Index);else Water.Remove(Index);
        MarkEdited(Position);RebuildQueue.Add(CellOf(Position));return true;
    }
    const FString Id=Kind==2 ? TEXT("minecraft:lava") : TEXT("minecraft:water");
    if(Kind && Existing==Id && LevelOf(State)==Level) return true;
    TArray<FBridgeBlock> Added;
    if(Kind && !AppendState(Position,Id,Kind==1 ? 0x3f76e4 : 0xffffff,FString::Printf(TEXT("level=%d"),Level),Added)) return false;
    const int32 Before=Rows->CountByPredicate([&](const FBridgeBlock& B){return OwnerOf(B)==Position;});
    if(Rows->Num()-Before+Added.Num()>8192 || Shapes-Before+Added.Num()>4194304) return false;
    const int32 Removed=Rows->RemoveAll([&](const FBridgeBlock& B){return OwnerOf(B)==Position;});Shapes-=Removed;
    Rows->Append(Added);Shapes+=Added.Num();
    const FIntVector Local=Position-CellOf(Position)*8;const uint16 Index=uint16(Local.X+Local.Z*8+Local.Y*64);
    auto& Water=WaterCells.FindOrAdd(CellOf(Position));if(Kind==1) Water.AddUnique(Index);else Water.Remove(Index);
    MarkEdited(Position);RebuildQueue.Add(CellOf(Position));return true;
}
void ABridgeWorld::TickFluids(float DeltaSeconds) {
    FluidClock+=FMath::Max(0.f,DeltaSeconds);TArray<FIntVector> Due;
    for(auto It=FluidUpdates.CreateIterator();It && Due.Num()<128;++It) if(It.Value()<=FluidClock) {Due.Add(It.Key());It.RemoveCurrent();}
    for(const auto& P:Due) {
        FString Id,State;GetBlockState(P,Id,State);int32 Kind=KindOf(Id),Level=LevelOf(State);
        if(!Id.IsEmpty() && !Kind) continue;
        if(Kind==2) {
            FString Under,UnderState;const FIntVector Below=P-FIntVector(0,1,0);
            if(GetBlockState(Below,Under,UnderState) && KindOf(Under)==1) {SetFluid(Below,0);PlaceBlock(Below,TEXT("minecraft:stone"),0xffffff);}
            bool Water=false;for(const FIntVector Offset:{FIntVector(1,0,0),FIntVector(-1,0,0),FIntVector(0,1,0),FIntVector(0,0,1),FIntVector(0,0,-1)}) {FString Other,S;Water|=GetBlockState(P+Offset,Other,S) && KindOf(Other)==1;}
            if(Water) {SetFluid(P,0);PlaceBlock(P,Level==0 ? TEXT("minecraft:obsidian") : TEXT("minecraft:cobblestone"),0xffffff);continue;}
        }
        if(Kind && Level==0) { // Stable sources feed their neighbors when edited.
            continue;
        }
        FString Above,AboveState;const bool FromAbove=GetBlockState(P+FIntVector(0,1,0),Above,AboveState) && KindOf(Above)>0;
        int32 IncomingKind=FromAbove ? KindOf(Above) : Kind,IncomingLevel=FromAbove ? 8 : 16,Sources=0;
        for(const FIntVector Offset:{FIntVector(1,0,0),FIntVector(-1,0,0),FIntVector(0,0,1),FIntVector(0,0,-1)}) {
            FString Other,S;if(!GetBlockState(P+Offset,Other,S) || !KindOf(Other)) continue;const int32 OtherKind=KindOf(Other);
            if(IncomingKind && IncomingKind!=OtherKind) continue;if(!IncomingKind) IncomingKind=OtherKind;
            const int32 OtherLevel=LevelOf(S);if(OtherLevel==0) ++Sources;
            if(!FromAbove) IncomingLevel=FMath::Min(IncomingLevel,(OtherLevel>=8 ? 0 : OtherLevel)+(OtherKind==2 && !FluidUltraWarm ? 2 : 1));
        }
        FString Below,BelowState;const bool Floor=GetBlockState(P-FIntVector(0,1,0),Below,BelowState) && !KindOf(Below);
        if(!FromAbove && IncomingKind==1 && Sources>=2 && Floor) IncomingLevel=0;
        if(IncomingKind && IncomingLevel<8+int32(FromAbove)) {if(Kind && Kind!=IncomingKind) continue;SetFluid(P,IncomingKind,IncomingLevel);}
        else if(Kind) SetFluid(P,0);
    }
}
