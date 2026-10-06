#include "BridgeProtocol.h"

namespace {
bool Number(const TSharedPtr<FJsonObject>& P, const TCHAR* Key, double Min, double Max, double& Out) {
    const auto* Field = P->Values.Find(Key);
    return Field && Field->IsValid() && (*Field)->Type == EJson::Number
        && P->TryGetNumberField(Key, Out) && FMath::IsFinite(Out) && Out >= Min && Out <= Max;
}
bool Integer(const TSharedPtr<FJsonObject>& P, const TCHAR* Key, double Min, double Max, uint64& Out) {
    double Value;
    if (!Number(P, Key, Min, Max, Value) || FMath::FloorToDouble(Value) != Value) return false;
    Out = uint64(Value); return true;
}
bool Guid(const TSharedPtr<FJsonObject>& P, const TCHAR* Key, FString& Out) {
    FGuid Value;
    return P->TryGetStringField(Key, Out) && Out.Len() == 36 && FGuid::ParseExact(Out, EGuidFormats::DigitsWithHyphens, Value);
}
bool Vector(const TSharedPtr<FJsonObject>& P, const TCHAR* X, const TCHAR* Y, const TCHAR* Z, FVector& Out, double Limit) {
    double A, B, C;
    if (!Number(P, X, -Limit, Limit, A) || !Number(P, Y, -Limit, Limit, B) || !Number(P, Z, -Limit, Limit, C)) return false;
    Out = FVector(A, B, C); return true;
}
bool Identifier(const FString& Value) {
    if(Value.Len()>128) return false;
    int32 Colon=INDEX_NONE;
    if(!Value.FindChar(TCHAR(':'),Colon) || Colon<=0 || Colon>=Value.Len()-1) return false;
    for(int32 I=0;I<Value.Len();++I) {
        const TCHAR C=Value[I]; if(I==Colon) continue;
        if(!((C>='a'&&C<='z') || (C>='0'&&C<='9') || C=='_' || C=='-' || C=='.' || (I>Colon&&C=='/'))) return false;
    }
    return true;
}
bool Selection(const TSharedPtr<FJsonObject>& P,FBridgePacket& R) {
    for(const auto* Name:{TEXT("heldItem"),TEXT("heldBlock")}) {
        FString Value;
        if(P->HasField(Name) && (!P->TryGetStringField(Name,Value) || (!Value.IsEmpty()&&!Identifier(Value)))) return false;
        if(FString(Name)==TEXT("heldItem")) R.HeldItem=Value; else R.HeldBlock=Value;
    }
    uint64 Color=0xffffff;
    if(P->HasField(TEXT("heldColor"))&&!Integer(P,TEXT("heldColor"),0,0xffffff,Color)) return false;
    R.HeldColor=int32(Color); return true;
}
}
bool BridgeProtocol::Parse(const TSharedPtr<FJsonObject>& P, FBridgePacket& Out) {
    if (!P.IsValid()) return false;
    FBridgePacket R; double Version; FString Kind;
    if (!Number(P, TEXT("v"), 1, 1, Version) || !P->TryGetStringField(TEXT("kind"), Kind)
        || !Guid(P, TEXT("session"), R.Session)
        || !Integer(P, TEXT("seq"), 1, 9007199254740991.0, R.Sequence)) return false;
    if (Kind==TEXT("feedback_ack")) {
        if(!Guid(P,TEXT("effectId"),R.EventId)) return false;
        R.Kind=EBridgeKind::FeedbackAck; Out=MoveTemp(R); return true;
    }
    if(!Vector(P, TEXT("x"), TEXT("y"), TEXT("z"), R.Position, 100000)) return false;
    if (Kind == TEXT("input")) {
        R.Kind = EBridgeKind::Input;
        if(!Selection(P,R)) return false;
        if(P->HasField(TEXT("sprint")) && (!P->HasTypedField<EJson::Boolean>(TEXT("sprint")) || !P->TryGetBoolField(TEXT("sprint"),R.Sprint))) return false;
        if (!Number(P, TEXT("yaw"), -1e9, 1e9, R.Yaw) || !Number(P, TEXT("pitch"), -90, 90, R.Pitch)
            || !Number(P, TEXT("forward"), -1, 1, R.Forward) || !Number(P, TEXT("right"), -1, 1, R.Right)
            || !P->HasTypedField<EJson::Boolean>(TEXT("jump")) || !P->TryGetBoolField(TEXT("jump"), R.Jump)) return false;
        if(P->HasField(TEXT("controller")) && (!P->HasTypedField<EJson::Boolean>(TEXT("controller")) || !P->TryGetBoolField(TEXT("controller"),R.Controller))) return false;
        if (P->HasField(TEXT("sneak")) && (!P->HasTypedField<EJson::Boolean>(TEXT("sneak")) || !P->TryGetBoolField(TEXT("sneak"), R.Sneak))) return false;
        if (P->HasField(TEXT("eyeHeight")) && !Number(P,TEXT("eyeHeight"),0.1,2.5,R.EyeHeight)) return false;
        if (P->HasField(TEXT("bodyHeight")) && !Number(P,TEXT("bodyHeight"),0.2,3,R.BodyHeight)) return false;
        uint64 Perspective=0,Layers=127;
        if(P->HasField(TEXT("perspective")) && !Integer(P,TEXT("perspective"),0,2,Perspective)) return false;
        if(P->HasField(TEXT("skinLayers")) && !Integer(P,TEXT("skinLayers"),0,127,Layers)) return false;
        R.Perspective=int32(Perspective);R.SkinLayers=int32(Layers);
        if(P->HasField(TEXT("swingProgress")) && !Number(P,TEXT("swingProgress"),0,1,R.SwingProgress)) return false;
        if(P->HasField(TEXT("equipProgress")) && !Number(P,TEXT("equipProgress"),0,1,R.EquipProgress)) return false;
        if(P->HasField(TEXT("useProgress")) && !Number(P,TEXT("useProgress"),0,1,R.UseProgress)) return false;
        if(P->HasField(TEXT("cameraFov")) && !Number(P,TEXT("cameraFov"),30,110,R.CameraFov)) return false;
        if(P->HasField(TEXT("usingItem")) && (!P->HasTypedField<EJson::Boolean>(TEXT("usingItem")) || !P->TryGetBoolField(TEXT("usingItem"),R.UsingItem))) return false;
        if(P->HasField(TEXT("leftHanded")) && (!P->HasTypedField<EJson::Boolean>(TEXT("leftHanded")) || !P->TryGetBoolField(TEXT("leftHanded"),R.LeftHanded))) return false;
        if(P->HasField(TEXT("slimArms")) && (!P->HasTypedField<EJson::Boolean>(TEXT("slimArms")) || !P->TryGetBoolField(TEXT("slimArms"),R.SlimArms))) return false;
        if(P->HasField(TEXT("useAction"))) {
            if(!P->TryGetStringField(TEXT("useAction"),R.UseAction)) return false;
            const TSet<FString> Actions{TEXT("none"),TEXT("eat"),TEXT("drink"),TEXT("block"),TEXT("bow"),TEXT("spear"),TEXT("trident"),TEXT("crossbow"),TEXT("spyglass"),TEXT("toot_horn"),TEXT("brush"),TEXT("bundle")};
            if(!Actions.Contains(R.UseAction)) return false;
        }
    } else if (Kind == TEXT("event")) {
        FString Event;
        if (!Guid(P, TEXT("eventId"), R.EventId) || !P->TryGetStringField(TEXT("event"), Event)) return false;
        if(Event==TEXT("player_respawn")) {
            if(!Guid(P,TEXT("importId"),R.ImportId)) return false;
            R.Kind=EBridgeKind::PlayerRespawn;
        }
        else if(Event==TEXT("mob_spawn") || Event==TEXT("mob_clear")) {
            if(!Guid(P,TEXT("importId"),R.ImportId)) return false;
            if(Event==TEXT("mob_clear")) R.Kind=EBridgeKind::MobClear;
            else {
                R.Kind=EBridgeKind::MobSpawn;R.Mob.Position=R.Position;
                if(!Guid(P,TEXT("mobId"),R.Mob.Id) || !P->TryGetStringField(TEXT("mobType"),R.Mob.Type) || !Identifier(R.Mob.Type)
                    || !P->TryGetStringField(TEXT("appearance"),R.Mob.Appearance) || R.Mob.Appearance.Len()!=64
                    || !Number(P,TEXT("yaw"),-1e9,1e9,R.Mob.Yaw)
                    || !P->HasTypedField<EJson::Boolean>(TEXT("hostile")) || !P->TryGetBoolField(TEXT("hostile"),R.Mob.Hostile)
                    || !P->HasTypedField<EJson::Boolean>(TEXT("baby")) || !P->TryGetBoolField(TEXT("baby"),R.Mob.Baby)) return false;
                for(const TCHAR C:R.Mob.Appearance) if(!((C>='0'&&C<='9') || (C>='a'&&C<='f'))) return false;
                double W,H,HP,MaxHP,Speed,Damage;
                if(!Number(P,TEXT("width"),.05,16,W) || !Number(P,TEXT("height"),.05,16,H)
                    || !Number(P,TEXT("health"),.01,10000,HP) || !Number(P,TEXT("maxHealth"),.01,10000,MaxHP) || HP>MaxHP
                    || !Number(P,TEXT("speed"),0,2,Speed) || !Number(P,TEXT("damage"),0,100,Damage)) return false;
                R.Mob.Width=float(W);R.Mob.Height=float(H);R.Mob.Health=float(HP);R.Mob.MaxHealth=float(MaxHP);
                R.Mob.Speed=float(Speed);R.Mob.Damage=float(Damage);
            }
        }
        else if(Event==TEXT("block_action")) {
            R.Kind=EBridgeKind::BlockAction;
            if(!Guid(P,TEXT("importId"),R.ImportId) || !P->TryGetStringField(TEXT("action"),R.Action)
                || (R.Action!=TEXT("break") && R.Action!=TEXT("place")) || !Selection(P,R)
                || !Number(P,TEXT("yaw"),-1e9,1e9,R.Yaw) || !Number(P,TEXT("pitch"),-90,90,R.Pitch)) return false;
            if(P->HasField(TEXT("sneak")) && (!P->HasTypedField<EJson::Boolean>(TEXT("sneak")) || !P->TryGetBoolField(TEXT("sneak"),R.Sneak))) return false;
        }
        else if (Event == TEXT("tnt_ignite")) R.Kind = EBridgeKind::Tnt;
        else if (Event == TEXT("bow_fire")) {
            R.Kind = EBridgeKind::Bow;
            if (!Vector(P, TEXT("dx"), TEXT("dy"), TEXT("dz"), R.Direction, 1)
                || !Number(P, TEXT("pull"), 0.1, 1, R.Pull)
                || !FMath::IsNearlyEqual(R.Direction.SizeSquared(), 1.0, 0.02)) return false;
        } else if (Event == TEXT("block_preview_clear")) R.Kind = EBridgeKind::ClearPreview;
        else if (Event == TEXT("world_clear")) R.Kind = EBridgeKind::WorldClear;
        else if(Event==TEXT("world_begin") || Event==TEXT("world_commit")) {
            if(!Guid(P,TEXT("importId"),R.ImportId)) return false;
            if(Event==TEXT("world_begin")) {
                if(!Vector(P,TEXT("ox"),TEXT("oy"),TEXT("oz"),R.MinecraftOrigin,30000000)) return false;
                R.Kind=EBridgeKind::WorldBegin;
            } else {
                uint64 Cells; if(!Integer(P,TEXT("cells"),27,245,Cells)) return false;
                R.ImportCells=int32(Cells); R.Kind=EBridgeKind::WorldCommit;
            }
        }
        else if (Event==TEXT("video_config")) {
            uint64 W,H,F,Q;
            if (!Integer(P,TEXT("width"),160,1920,W) || !Integer(P,TEXT("height"),90,1080,H)
                || !Integer(P,TEXT("fps"),1,60,F) || !Integer(P,TEXT("quality"),30,95,Q)
                || !Number(P,TEXT("exposure"),-6,6,R.VideoExposure)) return false;
            R.Kind=EBridgeKind::VideoConfig; R.VideoWidth=int32(W); R.VideoHeight=int32(H); R.VideoFps=int32(F); R.VideoQuality=int32(Q);
            if(P->HasField(TEXT("lighting")) && (!P->HasTypedField<EJson::Boolean>(TEXT("lighting")) || !P->TryGetBoolField(TEXT("lighting"),R.Lighting))) return false;
            if(P->HasField(TEXT("vanillaSky")) && (!P->HasTypedField<EJson::Boolean>(TEXT("vanillaSky")) || !P->TryGetBoolField(TEXT("vanillaSky"),R.VanillaSky))) return false;
            if(R.Lighting && R.VanillaSky) return false;
            if(P->HasField(TEXT("particleScale")) && !Number(P,TEXT("particleScale"),.25,2,R.ParticleScale)) return false;
            if(P->HasField(TEXT("particleDensity")) && !Number(P,TEXT("particleDensity"),.125,1,R.ParticleDensity)) return false;
            if(P->HasField(TEXT("particleLifetime")) && !Number(P,TEXT("particleLifetime"),.25,2,R.ParticleLifetime)) return false;
        }
        else if (Event == TEXT("world_scope")) {
            R.Kind = EBridgeKind::WorldScope; double X,Y,Z; uint64 Radius,Height;
            if (!Number(P,TEXT("cellX"),-4000000,4000000,X) || !Number(P,TEXT("cellY"),-4000000,4000000,Y)
                || !Number(P,TEXT("cellZ"),-4000000,4000000,Z) || FMath::FloorToDouble(X)!=X || FMath::FloorToDouble(Y)!=Y || FMath::FloorToDouble(Z)!=Z
                || !Integer(P,TEXT("radius"),1,3,Radius) || !Integer(P,TEXT("halfHeight"),1,2,Height)) return false;
            R.Cell = FIntVector(int32(X),int32(Y),int32(Z)); R.Radius=int32(Radius); R.HalfHeight=int32(Height);
        }
        else if (Event == TEXT("block_snapshot") || Event == TEXT("world_cell") || Event==TEXT("world_cell_textured") || Event==TEXT("world_cell_physics")) {
            const bool Physics=Event==TEXT("world_cell_physics");
            const bool Textured=Event==TEXT("world_cell_textured") || Physics;
            const bool WorldCell = Event == TEXT("world_cell") || Textured;
            R.Kind = WorldCell ? EBridgeKind::WorldCell : EBridgeKind::Snapshot; uint64 Index, Total;
            if (WorldCell) {
                double X,Y,Z;
                if (!Number(P,TEXT("cellX"),-4000000,4000000,X) || !Number(P,TEXT("cellY"),-4000000,4000000,Y)
                    || !Number(P,TEXT("cellZ"),-4000000,4000000,Z) || FMath::FloorToDouble(X)!=X || FMath::FloorToDouble(Y)!=Y || FMath::FloorToDouble(Z)!=Z) return false;
                R.Cell=FIntVector(int32(X),int32(Y),int32(Z));
            }
            const TArray<TSharedPtr<FJsonValue>>* Rows;
            if (!Guid(P, TEXT("snapshotId"), R.SnapshotId)
                || !Integer(P, TEXT("snapshotSeq"), 1, 9007199254740991.0, R.SnapshotSequence)
                || R.SnapshotSequence > R.Sequence
                || !Integer(P, TEXT("batchIndex"), 0, (Physics ? 8192 : (Textured ? 2048 : (WorldCell ? 1024 : MaxBatches))) - 1, Index)
                || !Integer(P, TEXT("totalBatches"), 1, Physics ? 8192 : (Textured ? 2048 : (WorldCell ? 1024 : MaxBatches)), Total) || Index >= Total
                || !P->TryGetArrayField(TEXT("blocks"), Rows) || Rows->Num() > (Textured ? 4 : (WorldCell ? 8 : 12))) return false;
            R.BatchIndex = int32(Index); R.TotalBatches = int32(Total);
            for (const auto& Row : *Rows) {
                const TArray<TSharedPtr<FJsonValue>>* Values;
                const int32 Fields = WorldCell ? 7 : 4;
                if (!Row.IsValid() || !Row->TryGetArray(Values) || (Values->Num() != Fields+(Textured ? 1 : 0)+(Physics ? 1 : 0) && !(Physics && (Values->Num()==12 || Values->Num()==14)))) return false;
                double V[7];
                for (int32 I = 0; I < Fields; ++I) if (!(*Values)[I].IsValid() || (*Values)[I]->Type != EJson::Number
                    || !(*Values)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I])) return false;
                if (FMath::Abs(V[0]) > 100000 || FMath::Abs(V[1]) > 100000 || FMath::Abs(V[2]) > 100000
                    || V[3] < 0 || V[3] > 0xffffff || FMath::FloorToDouble(V[3]) != V[3]) return false;
                FVector Size = FVector::OneVector;
                if (WorldCell) {
                    if (V[4]<=0 || V[5]<=0 || V[6]<=0 || V[4]>4 || V[5]>4 || V[6]>4) return false;
                    Size=FVector(V[4],V[5],V[6]);
                }
                FString BlockId;
                if(Textured) {
                    if(!(*Values)[7].IsValid() || (*Values)[7]->Type!=EJson::String || !(*Values)[7]->TryGetString(BlockId)
                        || BlockId.IsEmpty() || BlockId.Len()>128) return false;
                    int32 Colon=INDEX_NONE;
                    if(!BlockId.FindChar(TCHAR(':'),Colon) || Colon<=0 || Colon>=BlockId.Len()-1) return false;
                    for(int32 I=0;I<BlockId.Len();++I) {
                        const TCHAR C=BlockId[I];
                        if(I==Colon) continue;
                        if(!((C>='a' && C<='z') || (C>='0' && C<='9') || C=='_' || C=='-' || C=='.' || (I>Colon && C=='/'))) return false;
                    }
                }
                bool Collision=false;
                if(Physics && (!(*Values)[8].IsValid() || (*Values)[8]->Type!=EJson::Boolean || !(*Values)[8]->TryGetBool(Collision))) return false;
                FBridgeBlock Block{FVector(V[0], V[1], V[2]), int32(V[3]), Size, BlockId,Collision};
                if(Physics && Values->Num()>=12) {
                    FIntVector Owner;
                    for(int32 I=0;I<3;++I) {
                        double Value;
                        if(!(*Values)[9+I].IsValid() || (*Values)[9+I]->Type!=EJson::Number || !(*Values)[9+I]->TryGetNumber(Value)
                            || !FMath::IsFinite(Value) || FMath::Abs(Value)>30000000 || Value!=FMath::FloorToDouble(Value)) return false;
                        Owner[I]=int32(Value);
                    }
                    // An owner cannot edit a different cell than the one it was imported into.
                    const FIntVector OwnerCell(FMath::FloorToInt(Owner.X/8.0),FMath::FloorToInt(Owner.Y/8.0),FMath::FloorToInt(Owner.Z/8.0));
                    if(OwnerCell!=R.Cell) return false;
                    Block.SourceBlock=Owner;Block.HasSourceBlock=true;
                }
                if(Physics && Values->Num()==14) {
                    FString State; double Role;
                    if(!(*Values)[12].IsValid() || (*Values)[12]->Type!=EJson::String || !(*Values)[12]->TryGetString(State) || State.Len()>1024
                        || !(*Values)[13].IsValid() || (*Values)[13]->Type!=EJson::Number || !(*Values)[13]->TryGetNumber(Role)
                        || !FMath::IsFinite(Role) || Role!=FMath::FloorToDouble(Role) || Role<1 || Role>3) return false;
                    for(const TCHAR C:State) if(!((C>='a'&&C<='z') || (C>='0'&&C<='9') || C=='_' || C=='=' || C==',' || C=='.' || C=='-')) return false;
                    if((Role==1 || Role==3) && Collision) return false;
                    if(Role==2 && !Collision) return false;
                    Block.StateKey=MoveTemp(State); Block.Role=uint8(Role);
                }
                R.Blocks.Add(MoveTemp(Block));
            }
        } else return false;
    } else return false;
    Out = MoveTemp(R); return true;
}
