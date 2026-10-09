#include "BridgeWorld.h"
#include "BridgeBlockPalette.h"
#include "BridgeBlockPreview.h"
#include "BridgeLightingService.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"

namespace {
const FIntVector Directions[]={FIntVector(1,0,0),FIntVector(-1,0,0),FIntVector(0,1,0),FIntVector(0,-1,0),FIntVector(0,0,1),FIntVector(0,0,-1)};
TMap<FString,FString> Props(const FString& State) {TMap<FString,FString> Out;TArray<FString> Parts;State.ParseIntoArray(Parts,TEXT(","));for(const auto& Part:Parts) {FString Key,Value;if(Part.Split(TEXT("="),&Key,&Value)) Out.Add(Key,Value);}return Out;}
FString Key(const TMap<FString,FString>& Properties) {TArray<FString> Names;Properties.GetKeys(Names);Names.Sort();FString Out;for(const auto& Name:Names) {if(!Out.IsEmpty()) Out+=TEXT(",");Out+=Name+TEXT("=")+Properties[Name];}return Out;}
FIntVector Facing(const FString& Name) {return Name==TEXT("east") ? Directions[0] : Name==TEXT("west") ? Directions[1] : Name==TEXT("up") ? Directions[2] : Name==TEXT("down") ? Directions[3] : Name==TEXT("south") ? Directions[4] : Directions[5];}
bool Plant(const FString& Id) {return Id.EndsWith(TEXT("_sapling")) || Id.EndsWith(TEXT("_tulip")) || Id.EndsWith(TEXT("_orchid")) || Id.EndsWith(TEXT("_bush")) || Id.EndsWith(TEXT("_fungus")) || Id==TEXT("minecraft:dandelion") || Id==TEXT("minecraft:poppy") || Id==TEXT("minecraft:allium") || Id==TEXT("minecraft:azure_bluet") || Id==TEXT("minecraft:oxeye_daisy") || Id==TEXT("minecraft:cornflower") || Id==TEXT("minecraft:lily_of_the_valley") || Id==TEXT("minecraft:short_grass") || Id==TEXT("minecraft:fern") || Id==TEXT("minecraft:torchflower") || Id==TEXT("minecraft:wither_rose");}
bool Falling(const FString& Id) {return Id==TEXT("minecraft:sand") || Id==TEXT("minecraft:red_sand") || Id==TEXT("minecraft:gravel") || Id.EndsWith(TEXT("_concrete_powder")) || Id.EndsWith(TEXT("anvil"));}
bool Soil(const FString& Id) {return Id==TEXT("minecraft:grass_block") || Id==TEXT("minecraft:dirt") || Id==TEXT("minecraft:coarse_dirt") || Id==TEXT("minecraft:podzol") || Id==TEXT("minecraft:rooted_dirt") || Id==TEXT("minecraft:farmland") || Id==TEXT("minecraft:moss_block") || Id==TEXT("minecraft:mycelium") || Id.EndsWith(TEXT("_nylium")) || Id==TEXT("minecraft:mud");}
}
bool ABridgeWorld::SetNativeBlockState(const FIntVector& Block,const FString& Id,const FString& State,int32 Tint) {
    const FIntVector Cell=CellOf(Block);if(!Sealed || !Revisions.Contains(Cell)) return false;
    if(Id.IsEmpty() || Id==TEXT("minecraft:air")) return BreakBlock(Block);
    const auto* Before=FindVisual(Block);if(Before && Before->BlockId==Id && Before->StateKey==State) return true;
    TArray<FBridgeBlock> Rows;if(!AppendState(Block,Id,PlacementTint(Block,Id,Tint),State,Rows)) return false;
    auto* Data=Stored.Find(Cell);if(!Data) return false;
    int32 Old=0;for(const auto& Row:*Data) if(OwnerOf(Row)==Block) ++Old;
    if(Data->Num()-Old+Rows.Num()>8192 || Shapes-Old+Rows.Num()>4194304) return false;
    Data->RemoveAll([&](const FBridgeBlock& Row){return OwnerOf(Row)==Block;});Data->Append(Rows);Shapes+=Rows.Num()-Old;
    MarkEdited(Block);RebuildCell(Cell);UpdateConnections(Block);return true;
}
bool ABridgeWorld::ApplyNativeMutations(const TArray<FNativeMutation>& Changes) {
    // Build complete cell replacements first. Rendering/model/capacity failure
    // cannot leave a piston chain half moved, or duplicate the pushed block.
    if(!Sealed || Changes.IsEmpty()) return false;
    TMap<FIntVector,TArray<FBridgeBlock>> Draft;TSet<FIntVector> Owners;
    for(const auto& Change:Changes) {
        const FIntVector Cell=CellOf(Change.Block);const auto* Original=Stored.Find(Cell);
        if(!Original || !Revisions.Contains(Cell) || Owners.Contains(Change.Block)) return false;
        Owners.Add(Change.Block);if(!Draft.Contains(Cell)) Draft.Add(Cell,*Original);
        TArray<FBridgeBlock> Rows;
        if(!Change.Id.IsEmpty() && !AppendState(Change.Block,Change.Id,PlacementTint(Change.Block,Change.Id,Change.Tint),Change.State,Rows)) return false;
        auto& Data=Draft[Cell];Data.RemoveAll([&](const FBridgeBlock& Row){return OwnerOf(Row)==Change.Block;});Data.Append(Rows);
    }
    int64 NewShapes=Shapes;for(const auto& Cell:Draft) {if(Cell.Value.Num()>8192) return false;NewShapes+=Cell.Value.Num()-Stored[Cell.Key].Num();}
    if(NewShapes>4194304) return false;
    for(auto& Cell:Draft) {Shapes+=Cell.Value.Num()-Stored[Cell.Key].Num();Stored[Cell.Key]=MoveTemp(Cell.Value);IndexVisualCell(Cell.Key);}
    for(const auto& Change:Changes) {
        const FIntVector Cell=CellOf(Change.Block),Local=Change.Block-Cell*8;
        WaterCells.FindOrAdd(Cell).Remove(uint16(Local.X+Local.Z*8+Local.Y*64));MarkEdited(Change.Block,false);
    }
    for(const auto& Cell:Draft) RebuildCell(Cell.Key);
    for(const auto& Change:Changes) UpdateConnections(Change.Block);
    return true;
}
void ABridgeWorld::EnableNativeRules(int32 RandomTicks) {
    NativeRules=true;NativeRandomTickSpeed=FMath::Clamp(RandomTicks,0,4096);GrassSections.Empty();SortedGrassSections.Empty();GrassSectionCursor=0;
    for(const auto& Cell:Stored) for(const auto& Row:Cell.Value) if(Row.Role<=1) {
        const FIntVector V=OwnerOf(Row);
        if(Row.BlockId==TEXT("minecraft:grass_block")) GrassSections.Add(FIntVector(FMath::FloorToInt(V.X/16.),FMath::FloorToInt(V.Y/16.),FMath::FloorToInt(V.Z/16.)));
        if(Row.BlockId==TEXT("minecraft:observer") && Row.StateKey.Contains(TEXT("powered=true"))) RuleDelayed.Add(V,NativeRuleTick+2);
        if(Falling(Row.BlockId) || Plant(Row.BlockId) || Row.BlockId.Contains(TEXT("redstone")) || Row.BlockId==TEXT("minecraft:lever") || Row.BlockId==TEXT("minecraft:repeater") || Row.BlockId==TEXT("minecraft:comparator") || Row.BlockId==TEXT("minecraft:observer") || Row.BlockId==TEXT("minecraft:hopper") || Row.BlockId==TEXT("minecraft:dropper") || Row.BlockId.Contains(TEXT("piston")) || Row.StateKey.Contains(TEXT("powered="))) RuleQueue.Add(V);
    }
}
void ABridgeWorld::QueueNativeRule(const FIntVector& Block) {
    for(const auto& D:Directions) {const FIntVector Neighbor=Block+D;FString Id,State;
        if(GetBlockState(Neighbor,Id,State) && Id==TEXT("minecraft:observer") && Neighbor+Facing(Props(State).FindRef(TEXT("facing")))==Block && !RuleDelayed.Contains(Neighbor)) RuleDelayed.Add(Neighbor,NativeRuleTick+2);
    }
    RuleQueue.Add(Block);for(const auto& D:Directions) {RuleQueue.Add(Block+D);for(int32 DY:{-1,1}) if(D.Y==0) RuleQueue.Add(Block+D+FIntVector(0,DY,0));}
    FString Id,State;if(GetBlockState(Block,Id,State) && Id==TEXT("minecraft:grass_block")) GrassSections.Add(FIntVector(FMath::FloorToInt(Block.X/16.),FMath::FloorToInt(Block.Y/16.),FMath::FloorToInt(Block.Z/16.)));
}
int32 ABridgeWorld::NativeSignal(const FIntVector& Source,const FIntVector& Target,bool Wire) const {
    FString Id,State;if(!GetBlockState(Source,Id,State)) return 0;const auto P=Props(State);
    if(Id==TEXT("minecraft:redstone_block")) return 15;
    if(Id==TEXT("minecraft:redstone_wire")) return Wire ? FMath::Clamp(FCString::Atoi(*P.FindRef(TEXT("power"))),0,15) : 0;
    if(Id.Contains(TEXT("redstone_torch")) && P.FindRef(TEXT("lit"))!=TEXT("false")) {
        const FIntVector Attached=Id==TEXT("minecraft:redstone_wall_torch") ? Source-Facing(P.FindRef(TEXT("facing"))) : Source-FIntVector(0,1,0);return Target==Attached ? 0 : 15;
    }
    // Gate FACING points toward its input (AbstractRedstoneGateBlock.getPower).
    // Observer FACING points toward the block it observes; output is opposite.
    if(Id==TEXT("minecraft:repeater") || Id==TEXT("minecraft:observer")) return P.FindRef(TEXT("powered"))==TEXT("true") && Target==Source-Facing(P.FindRef(TEXT("facing"))) ? 15 : 0;
    if(Id==TEXT("minecraft:comparator")) return Target==Source-Facing(P.FindRef(TEXT("facing"))) ? ComparatorPower.FindRef(Source) : 0;
    if(Id==TEXT("minecraft:lever") || Id.EndsWith(TEXT("_button")) || Id.EndsWith(TEXT("_pressure_plate"))) return P.FindRef(TEXT("powered"))==TEXT("true") ? 15 : FMath::Clamp(FCString::Atoi(*P.FindRef(TEXT("power"))),0,15);
    if(Id==TEXT("minecraft:daylight_detector")) return FMath::Clamp(FCString::Atoi(*P.FindRef(TEXT("power"))),0,15);
    return 0;
}
int32 ABridgeWorld::NativePowerAt(const FIntVector& Block,bool Wire,const FIntVector* Ignore) const {
    int32 Power=0;for(const auto& D:Directions) {const FIntVector Neighbor=Block+D;if(Ignore && Neighbor==*Ignore) continue;
        Power=FMath::Max(Power,NativeSignal(Neighbor,Block,Wire));
        if(IsOpaqueVoxel(Neighbor)) for(const auto& Inner:Directions) {const FIntVector Source=Neighbor+Inner;if(Source==Block || Ignore && Source==*Ignore) continue;
            FString Id,State;if(!GetBlockState(Source,Id,State)) continue;
            if(Id!=TEXT("minecraft:redstone_wire")) Power=FMath::Max(Power,NativeSignal(Source,Neighbor,false));
        }
    }return Power;
}
void ABridgeWorld::UpdateNativeRule(const FIntVector& Block,bool Delayed) {
    FString Id,State;if(!GetBlockState(Block,Id,State)) return;auto P=Props(State);FColor Tint=FColor::White;FString Ignored;GetBlockInfo(Block,Ignored,Tint);
    auto Apply=[&](){SetNativeBlockState(Block,Id,Key(P),Tint.ToPackedARGB()&0xffffffu);};
    const FIntVector Below=Block-FIntVector(0,1,0);FString Support,SupportState;const bool KnownBelow=Revisions.Contains(CellOf(Below));GetBlockState(Below,Support,SupportState);
    if(Falling(Id) && KnownBelow && (Support.IsEmpty() || Support==TEXT("minecraft:water") || Support==TEXT("minecraft:lava") || Plant(Support))) {StartNativeFall(Block,Id,State,Tint.ToPackedARGB()&0xffffffu);return;}
    if(Plant(Id) && KnownBelow && !Soil(Support)) {
        // Keep unsupported plants until the item entity can be represented.
        if(NativeRuleDrop && NativeRuleDrop(Id,BlockCenter(Block))) BreakBlock(Block);
        else RuleDelayed.Add(Block,NativeRuleTick+20);return;
    }
    if(Id==TEXT("minecraft:hopper") && NativeHopperTransfer) {
        const bool Enabled=NativePowerAt(Block,true)==0;P.Add(TEXT("enabled"),Enabled ? TEXT("true") : TEXT("false"));Apply();
        if(!Delayed && RuleDelayed.Contains(Block)) return;
        const bool Moved=Enabled && NativeHopperTransfer(Block,Facing(P.FindRef(TEXT("facing"))));
        RuleDelayed.Add(Block,NativeRuleTick+(Moved ? 8 : 1));return;
    }
    if(Id==TEXT("minecraft:dropper") && NativeDropperEmit) {
        const bool Powered=NativePowerAt(Block,true)>0;
        if(Delayed) {NativeDropperEmit(Block,Facing(P.FindRef(TEXT("facing"))));return;}
        if(Powered && P.FindRef(TEXT("triggered"))!=TEXT("true")) {P.Add(TEXT("triggered"),TEXT("true"));Apply();RuleDelayed.Add(Block,NativeRuleTick+4);}
        else if(!Powered && P.FindRef(TEXT("triggered"))==TEXT("true")) {P.Add(TEXT("triggered"),TEXT("false"));Apply();}return;
    }
    if(Id.EndsWith(TEXT("_pressure_plate")) && NativePlateOccupied) {const bool Occupied=NativePlateOccupied(Block);if(P.Contains(TEXT("powered"))) P.Add(TEXT("powered"),Occupied ? TEXT("true") : TEXT("false"));else P.Add(TEXT("power"),Occupied ? TEXT("15") : TEXT("0"));Apply();RuleDelayed.Add(Block,NativeRuleTick+20);return;}
    if(Id==TEXT("minecraft:redstone_wire")) {
        int32 Power=NativePowerAt(Block,false);for(const auto& D:Directions) if(D.Y==0) {
            Power=FMath::Max(Power,NativeSignal(Block+D,Block,true)-1);
            // Wire can climb a solid step only when the space above it is open.
            if(!IsOpaqueVoxel(Block+FIntVector(0,1,0))) Power=FMath::Max(Power,NativeSignal(Block+D+FIntVector(0,1,0),Block,true)-1);
            if(!IsOpaqueVoxel(Block+D)) Power=FMath::Max(Power,NativeSignal(Block+D-FIntVector(0,1,0),Block,true)-1);
        }P.Add(TEXT("power"),FString::FromInt(FMath::Clamp(Power,0,15)));Apply();return;
    }
    if(Id.Contains(TEXT("redstone_torch"))) {
        const FIntVector Attached=Id==TEXT("minecraft:redstone_wall_torch") ? Block-Facing(P.FindRef(TEXT("facing"))) : Below;
        const bool Lit=NativePowerAt(Attached,true,&Block)==0;
        if((P.FindRef(TEXT("lit"))!=TEXT("false"))!=Lit) {if(!Delayed) {if(!RuleDelayed.Contains(Block)) RuleDelayed.Add(Block,NativeRuleTick+2);}else {P.Add(TEXT("lit"),Lit ? TEXT("true") : TEXT("false"));Apply();}}return;
    }
    if(Id==TEXT("minecraft:repeater") || Id==TEXT("minecraft:comparator")) {
        const FIntVector Direction=Facing(P.FindRef(TEXT("facing")));const FIntVector Back=Block+Direction;int32 Input=NativeSignal(Back,Block,true);
        if(IsOpaqueVoxel(Back)) Input=FMath::Max(Input,NativePowerAt(Back,true,&Block));
        const FIntVector Side(-Direction.Z,0,Direction.X);int32 SidePower=FMath::Max(NativeSignal(Block+Side,Block,true),NativeSignal(Block-Side,Block,true));
        if(Id==TEXT("minecraft:repeater")) {
            bool Locked=false;for(const auto& S:{Side,FIntVector(-Side.X,-Side.Y,-Side.Z)}) {FString SideId,SideState;if(GetBlockState(Block+S,SideId,SideState) && (SideId==TEXT("minecraft:repeater") || SideId==TEXT("minecraft:comparator")) && NativeSignal(Block+S,Block,true)>0) Locked=true;}
            P.Add(TEXT("locked"),Locked ? TEXT("true") : TEXT("false"));Apply();if(Locked) return;
        } else {if(NativeContainerPower) Input=FMath::Max(Input,NativeContainerPower(Back));Input=P.FindRef(TEXT("mode"))==TEXT("subtract") ? FMath::Max(0,Input-SidePower) : (Input>=SidePower ? Input : 0);}
        const bool Powered=Input>0;
        if((P.FindRef(TEXT("powered"))==TEXT("true"))!=Powered || Id==TEXT("minecraft:comparator") && ComparatorPower.FindRef(Block)!=Input) {
            if(!Delayed) {if(!RuleDelayed.Contains(Block)) RuleDelayed.Add(Block,NativeRuleTick+(Id==TEXT("minecraft:repeater") ? FMath::Max(1,FCString::Atoi(*P.FindRef(TEXT("delay"))))*2 : 2));}
            else {P.Add(TEXT("powered"),Powered ? TEXT("true") : TEXT("false"));if(Id==TEXT("minecraft:comparator")) ComparatorPower.Add(Block,Input);Apply();QueueNativeRule(Block);}
        }
        // Inventory contents can change without a neighboring block mutation.
        if(Id==TEXT("minecraft:comparator") && !RuleDelayed.Contains(Block)) RuleDelayed.Add(Block,NativeRuleTick+2);
        return;
    }
    if(Id==TEXT("minecraft:observer")) {
        if(Delayed) {const bool Was=P.FindRef(TEXT("powered"))==TEXT("true");P.Add(TEXT("powered"),Was ? TEXT("false") : TEXT("true"));Apply();if(!Was) RuleDelayed.Add(Block,NativeRuleTick+2);}return;
    }
    const FIntVector OutputFace=Facing(P.FindRef(TEXT("facing")));
    const FIntVector IgnoredFront=Block+OutputFace;
    bool Powered=NativePowerAt(Block,true,(Id==TEXT("minecraft:piston") || Id==TEXT("minecraft:sticky_piston")) ? &IgnoredFront : nullptr)>0;
    if(Id.EndsWith(TEXT("_door"))) Powered=Powered || NativePowerAt(Block+FIntVector(0,P.FindRef(TEXT("half"))==TEXT("upper") ? -1 : 1,0),true)>0;
    if(Id==TEXT("minecraft:piston") || Id==TEXT("minecraft:sticky_piston")) {
        const bool Extended=P.FindRef(TEXT("extended"))==TEXT("true");if(Extended==Powered) return;
        const FIntVector Direction=Facing(P.FindRef(TEXT("facing"))),Front=Block+Direction;
        if(!Delayed) {if(!RuleDelayed.Contains(Block)) RuleDelayed.Add(Block,NativeRuleTick+1);return;}
        auto Movable=[](const FString& Name) {return Name!=TEXT("minecraft:obsidian") && Name!=TEXT("minecraft:crying_obsidian") && Name!=TEXT("minecraft:bedrock") && Name!=TEXT("minecraft:reinforced_deepslate") && Name!=TEXT("minecraft:spawner") && Name!=TEXT("minecraft:chest") && Name!=TEXT("minecraft:trapped_chest") && Name!=TEXT("minecraft:barrel") && Name!=TEXT("minecraft:furnace") && Name!=TEXT("minecraft:blast_furnace") && Name!=TEXT("minecraft:smoker") && Name!=TEXT("minecraft:hopper") && Name!=TEXT("minecraft:dispenser") && Name!=TEXT("minecraft:dropper") && Name!=TEXT("minecraft:brewing_stand") && !Name.EndsWith(TEXT("shulker_box")) && !Name.EndsWith(TEXT("_door")) && !Name.EndsWith(TEXT("_bed")) && Name!=TEXT("minecraft:piston_head") && Name!=TEXT("minecraft:moving_piston");};
        TArray<FNativeMutation> Changes;
        if(Powered) {
            struct FPush {FIntVector Position;FString Id,State;int32 Tint;};TArray<FPush> Push;
            for(int32 N=0;N<=12;++N) {const FIntVector Position=Front+Direction*N;if(!Revisions.Contains(CellOf(Position))) return;
                FString PushId,PushState;FColor PushTint=FColor::White;if(!GetBlockState(Position,PushId,PushState) || PushId==TEXT("minecraft:water") || PushId==TEXT("minecraft:lava")) break;
                if(N==12 || !Movable(PushId) || PushState.Contains(TEXT("extended=true"))) return;
                TArray<FBridgeBlock> Draft;if(!AppendState(Position+Direction,PushId,0xffffff,PushState,Draft)) return;
                GetBlockInfo(Position,PushId,PushTint);Push.Add({Position,PushId,PushState,int32(PushTint.ToPackedARGB()&0xffffffu)});
            }
            const FString HeadState=TEXT("facing=")+P.FindRef(TEXT("facing"))+TEXT(",short=false,type=")+(Id==TEXT("minecraft:sticky_piston") ? TEXT("sticky") : TEXT("normal"));
            for(int32 N=Push.Num()-1;N>=0;--N) Changes.Add({Push[N].Position+Direction,Push[N].Id,Push[N].State,Push[N].Tint});
            Changes.Add({Front,TEXT("minecraft:piston_head"),HeadState,0xffffff});
        } else {
            FString HeadId,HeadState;if(GetBlockState(Front,HeadId,HeadState) && HeadId!=TEXT("minecraft:piston_head")) return;
            Changes.Add({Front,FString(),FString(),0xffffff});
            if(Id==TEXT("minecraft:sticky_piston")) {FString PullId,PullState;FColor PullTint=FColor::White;if(GetBlockState(Front+Direction,PullId,PullState) && Movable(PullId)) {
                GetBlockInfo(Front+Direction,PullId,PullTint);Changes[0]={Front,PullId,PullState,int32(PullTint.ToPackedARGB()&0xffffffu)};
                Changes.Add({Front+Direction,FString(),FString(),0xffffff});
            }}
        }
        P.Add(TEXT("extended"),Powered ? TEXT("true") : TEXT("false"));Changes.Add({Block,Id,Key(P),int32(Tint.ToPackedARGB()&0xffffffu)});
        ApplyNativeMutations(Changes);return;
    }
    if(Id==TEXT("minecraft:tnt") && Powered && NativePrimeTnt) {NativePrimeTnt(Block);return;}
    if(Id==TEXT("minecraft:redstone_lamp")) {
        if(Powered) {P.Add(TEXT("lit"),TEXT("true"));Apply();}
        else if(P.FindRef(TEXT("lit"))==TEXT("true")) {if(Delayed) {P.Add(TEXT("lit"),TEXT("false"));Apply();}else if(!RuleDelayed.Contains(Block)) RuleDelayed.Add(Block,NativeRuleTick+4);}return;
    }
    if((Id.EndsWith(TEXT("_door")) || Id.EndsWith(TEXT("_trapdoor")) || Id.EndsWith(TEXT("_fence_gate"))) && P.Contains(TEXT("powered"))) {
        if((P.FindRef(TEXT("powered"))==TEXT("true"))!=Powered) {
            P.Add(TEXT("powered"),Powered ? TEXT("true") : TEXT("false"));P.Add(TEXT("open"),Powered ? TEXT("true") : TEXT("false"));
            const FIntVector Partner=Block+FIntVector(0,P.FindRef(TEXT("half"))==TEXT("upper") ? -1 : 1,0);FString OtherId,OtherState;
            if(Id.EndsWith(TEXT("_door")) && GetBlockState(Partner,OtherId,OtherState) && OtherId==Id) {
                auto Other=Props(OtherState);Other.Add(TEXT("powered"),P[TEXT("powered")]);Other.Add(TEXT("open"),P[TEXT("open")]);
                ApplyNativeMutations({{Block,Id,Key(P),int32(Tint.ToPackedARGB()&0xffffffu)},{Partner,Id,Key(Other),int32(Tint.ToPackedARGB()&0xffffffu)}});
            } else Apply();
        }return;
    }
}
bool ABridgeWorld::StartNativeFall(const FIntVector& Block,const FString& Id,const FString& State,int32 Tint) {
    if(NativeFalls.Num()>=128 || !GetWorld()) return false;
    FActorSpawnParameters Spawn;Spawn.Owner=this;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Position=BlockCenter(Block);auto* Visual=GetWorld()->SpawnActor<ABridgeBlockPreview>(Position,FRotator::ZeroRotator,Spawn);if(!Visual) return false;
    FBridgeBlock Row;Row.BlockId=Id;Row.StateKey=State;Row.Position=FVector::ZeroVector;Row.Role=1;Row.Color=Tint;
    Visual->Replace({Row},Position,SavedMaterial,SavedPalette,false);if(!Visual->HasContent() || !BreakBlock(Block)) {Visual->Destroy();return false;}
    FNativeFall Fall;Fall.Source=Block;Fall.Id=Id;Fall.State=State;Fall.Tint=Tint;Fall.Position=Fall.Previous=Position;Fall.Visual=Visual;NativeFalls.Add(Fall);return true;
}
void ABridgeWorld::TickNativeRules(float DeltaSeconds) {
    NativeRuleClock+=FMath::Max(0.f,DeltaSeconds);int32 Steps=0;
    while(NativeRuleClock>=.05f && ++Steps<=20) {
        NativeRuleClock-=.05f;++NativeRuleTick;
        TArray<FIntVector> Due;for(auto It=RuleDelayed.CreateIterator();It;++It) if(It.Value()<=NativeRuleTick) {Due.Add(It.Key());It.RemoveCurrent();}
        Due.Sort([](const FIntVector& A,const FIntVector& B){return A.X!=B.X ? A.X<B.X : A.Y!=B.Y ? A.Y<B.Y : A.Z<B.Z;});
        for(const auto& Block:Due) UpdateNativeRule(Block,true);
        // Bounded propagation preserves pending work across ticks.
        for(int32 Work=0;Work<4096 && !RuleQueue.IsEmpty();++Work) {const FIntVector Block=*RuleQueue.CreateConstIterator();RuleQueue.Remove(Block);UpdateNativeRule(Block);}
        if(SortedGrassSections.Num()!=GrassSections.Num()) {
            SortedGrassSections=GrassSections.Array();
            SortedGrassSections.Sort([](const FIntVector& A,const FIntVector& B){return A.X!=B.X ? A.X<B.X : A.Y!=B.Y ? A.Y<B.Y : A.Z<B.Z;});
        }
        const auto& Sections=SortedGrassSections;
        // Normal randomTickSpeed=3 visits every section of the standard export.
        // Large custom rates share a bounded budget round-robin across ticks.
        const int32 Visits=NativeRandomTickSpeed>0 ? FMath::Min(Sections.Num(),FMath::Max(1,8192/NativeRandomTickSpeed)) : 0;
        for(int32 SectionIndex=0;SectionIndex<Visits;++SectionIndex) {
        const auto& Section=Sections[(GrassSectionCursor+SectionIndex)%Sections.Num()];
        for(int32 Attempt=0;Attempt<NativeRandomTickSpeed;++Attempt) {
            const FIntVector Block=Section*16+FIntVector(RuleRandom.RandRange(0,15),RuleRandom.RandRange(0,15),RuleRandom.RandRange(0,15));
            FString Id,State;if(!GetBlockState(Block,Id,State) || Id!=TEXT("minecraft:grass_block")) continue;
            const FIntVector Above=Block+FIntVector(0,1,0);if(!Revisions.Contains(CellOf(Above))) continue;
            FString Top,TopState;GetBlockState(Above,Top,TopState);
            const bool Snow=Top==TEXT("minecraft:snow") && Props(TopState).FindRef(TEXT("layers"))==TEXT("1");
            if(!Snow && (IsOpaqueVoxel(Above) || Top==TEXT("minecraft:water") && TopState==TEXT("level=0"))) {SetNativeBlockState(Block,TEXT("minecraft:dirt"),SavedPalette ? SavedPalette->DefaultState(TEXT("minecraft:dirt")) : FString());continue;}
            const FLinearColor Light=Lighting ? Lighting->Sample(Above) : FLinearColor(1,0,1,1);
            if(FMath::Max(FMath::RoundToInt(Light.R*15)-NativeSkyDarkness,FMath::RoundToInt(Light.G*15))<9) continue;
            for(int32 I=0;I<4;++I) {const FIntVector Target=Block+FIntVector(RuleRandom.RandRange(-1,1),RuleRandom.RandRange(-3,1),RuleRandom.RandRange(-1,1));
                FString Dirt,DirtState,Over,OverState;if(!GetBlockState(Target,Dirt,DirtState) || Dirt!=TEXT("minecraft:dirt") || !Revisions.Contains(CellOf(Target+FIntVector(0,1,0)))) continue;
                GetBlockState(Target+FIntVector(0,1,0),Over,OverState);
                if(!IsOpaqueVoxel(Target+FIntVector(0,1,0)) && Over!=TEXT("minecraft:water") && Over!=TEXT("minecraft:lava")) SetNativeBlockState(Target,TEXT("minecraft:grass_block"),Over==TEXT("minecraft:snow") || Over==TEXT("minecraft:snow_block") ? TEXT("snowy=true") : TEXT("snowy=false"));
            }
        }}
        if(!Sections.IsEmpty()) GrassSectionCursor=(GrassSectionCursor+Visits)%Sections.Num();
        for(int32 I=NativeFalls.Num()-1;I>=0;--I) {auto& Fall=NativeFalls[I];++Fall.Age;Fall.Previous=Fall.Position;Fall.Velocity.Z-=80;
            EnsureCollisionForPosition(Fall.Position);FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(NativeFalling),false,this);
            const FVector Delta=Fall.Velocity*.05f;
            const bool Collided=GetWorld()->SweepSingleByChannel(Hit,Fall.Position,Fall.Position+Delta,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeBox(FVector(49)),Query);
            Fall.Position+=Delta*(Collided ? FMath::Max(0.f,Hit.Time-.001f) : 1.f);Fall.Velocity*=.98f;
            const FIntVector Target=SourceVoxelAt(Fall.Position-FVector(0,0,45));FString Id,State;GetBlockState(Target,Id,State);
            bool Harden=Fall.Id.EndsWith(TEXT("_concrete_powder")) && (Id==TEXT("minecraft:water") || IsWaterAtUEPosition(Fall.Position));
            if((Collided && Hit.Normal.Z>.6f) || Harden || Fall.Age>600 || !ContainsUEPosition(Fall.Position)) {
                if(Harden) {Fall.Id.LeftChopInline(7);Fall.State=SavedPalette ? SavedPalette->DefaultState(Fall.Id) : FString();}
                const FIntVector Land=Collided ? SourceVoxelAt(Hit.ImpactPoint+FVector(0,0,1)) : Target;FString Existing,ExistingState;GetBlockState(Land,Existing,ExistingState);
                const bool Replaceable=Existing.IsEmpty() || Existing==TEXT("minecraft:water") || Existing==TEXT("minecraft:lava") || Plant(Existing);
                bool Placed=false;if(Fall.Age<=600 && Replaceable) Placed=SetNativeBlockState(Land,Fall.Id,Fall.State,Fall.Tint);
                if(!Placed && (!NativeRuleDrop || !NativeRuleDrop(Fall.Id,Fall.Position))) {
                    // Retry with its inventory quantity still owned by this entity.
                    if(!ContainsUEPosition(Fall.Position)) Fall.Position=Fall.Previous;
                    Fall.Velocity=FVector::ZeroVector;Fall.Age=FMath::Min(Fall.Age,600);continue;
                }
                if(Fall.Visual.IsValid()) Fall.Visual->Destroy();NativeFalls.RemoveAtSwap(I);
            }
        }
    }
    const float Alpha=FMath::Clamp(NativeRuleClock/.05f,0.f,1.f);for(auto& Fall:NativeFalls) if(Fall.Visual.IsValid()) Fall.Visual->SetActorLocation(FMath::Lerp(Fall.Previous,Fall.Position,Alpha));
}
TArray<TSharedPtr<FJsonValue>> ABridgeWorld::ExportNativeFalling() const {
    TArray<TSharedPtr<FJsonValue>> Out;for(const auto& Fall:NativeFalls) {auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),Fall.Id);Row->SetStringField(TEXT("state"),Fall.State);Row->SetNumberField(TEXT("tint"),Fall.Tint);Row->SetNumberField(TEXT("age"),Fall.Age);
        const FVector P=ImportOrigin+FVector(-(Fall.Position-ImportAnchor).Y,(Fall.Position-ImportAnchor).Z,(Fall.Position-ImportAnchor).X)/100;
        Row->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});
        Row->SetArrayField(TEXT("velocity"),{MakeShared<FJsonValueNumber>(Fall.Velocity.X),MakeShared<FJsonValueNumber>(Fall.Velocity.Y),MakeShared<FJsonValueNumber>(Fall.Velocity.Z)});Out.Add(MakeShared<FJsonValueObject>(Row));}return Out;
}
bool ABridgeWorld::ImportNativeFalling(const TArray<TSharedPtr<FJsonValue>>& Values) {
    if(Values.Num()>128 || !NativeFalls.IsEmpty()) return false;
    TArray<FNativeFall> Draft;
    for(const auto& Value:Values) {const TSharedPtr<FJsonObject>* Row=nullptr;double Tint=0,Age=0;FNativeFall Fall;
        if(!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("id"),Fall.Id) || !Falling(Fall.Id) || !(*Row)->TryGetStringField(TEXT("state"),Fall.State) || !(*Row)->TryGetNumberField(TEXT("tint"),Tint) || !(*Row)->TryGetNumberField(TEXT("age"),Age)
            || !FMath::IsFinite(Tint) || Tint<0 || Tint>0xffffff || !FMath::IsFinite(Age) || Age<0 || Age>600) return false;
        auto Read=[&](const TCHAR* Name,FVector& Vector) {const TArray<TSharedPtr<FJsonValue>>* Coordinates=nullptr;if(!(*Row)->TryGetArrayField(Name,Coordinates) || Coordinates->Num()!=3) return false;double Components[3];for(int32 I=0;I<3;++I) if(!(*Coordinates)[I]->TryGetNumber(Components[I]) || !FMath::IsFinite(Components[I]) || FMath::Abs(Components[I])>30000000) return false;Vector=FVector(Components[0],Components[1],Components[2]);return true;};
        if(!Read(TEXT("position"),Fall.Position) || !Read(TEXT("velocity"),Fall.Velocity)) return false;
        Fall.Position=BridgeProtocol::ToUnreal(Fall.Position-ImportOrigin,ImportAnchor);if(!ContainsUEPosition(Fall.Position)) return false;
        Fall.Previous=Fall.Position;Fall.Tint=int32(Tint);Fall.Age=int32(Age);Draft.Add(Fall);
    }
    for(auto& Fall:Draft) {
        FActorSpawnParameters Spawn;Spawn.Owner=this;auto* Visual=GetWorld()->SpawnActor<ABridgeBlockPreview>(Fall.Position,FRotator::ZeroRotator,Spawn);
        if(!Visual) {for(auto& Other:Draft) if(Other.Visual.IsValid()) Other.Visual->Destroy();return false;}
        FBridgeBlock Row;Row.BlockId=Fall.Id;Row.StateKey=Fall.State;Row.Color=Fall.Tint;Row.Role=1;Visual->Replace({Row},Fall.Position,SavedMaterial,SavedPalette,false);Fall.Visual=Visual;
        if(!Visual->HasContent()) {for(auto& Other:Draft) if(Other.Visual.IsValid()) Other.Visual->Destroy();return false;}
    }
    NativeFalls=MoveTemp(Draft);return true;
}
