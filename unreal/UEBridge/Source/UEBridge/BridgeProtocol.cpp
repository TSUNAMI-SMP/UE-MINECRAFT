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
}
bool BridgeProtocol::Parse(const TSharedPtr<FJsonObject>& P, FBridgePacket& Out) {
    if (!P.IsValid()) return false;
    FBridgePacket R; double Version; FString Kind;
    if (!Number(P, TEXT("v"), 1, 1, Version) || !P->TryGetStringField(TEXT("kind"), Kind)
        || !Guid(P, TEXT("session"), R.Session)
        || !Integer(P, TEXT("seq"), 1, 9007199254740991.0, R.Sequence)
        || !Vector(P, TEXT("x"), TEXT("y"), TEXT("z"), R.Position, 100000)) return false;
    if (Kind == TEXT("input")) {
        R.Kind = EBridgeKind::Input;
        if (!Number(P, TEXT("yaw"), -1e9, 1e9, R.Yaw) || !Number(P, TEXT("pitch"), -90, 90, R.Pitch)
            || !Number(P, TEXT("forward"), -1, 1, R.Forward) || !Number(P, TEXT("right"), -1, 1, R.Right)
            || !P->HasTypedField<EJson::Boolean>(TEXT("jump")) || !P->TryGetBoolField(TEXT("jump"), R.Jump)) return false;
        if (P->HasField(TEXT("sneak")) && (!P->HasTypedField<EJson::Boolean>(TEXT("sneak")) || !P->TryGetBoolField(TEXT("sneak"), R.Sneak))) return false;
        if (P->HasField(TEXT("eyeHeight")) && !Number(P,TEXT("eyeHeight"),0.1,2.5,R.EyeHeight)) return false;
        if (P->HasField(TEXT("bodyHeight")) && !Number(P,TEXT("bodyHeight"),0.2,3,R.BodyHeight)) return false;
    } else if (Kind == TEXT("event")) {
        FString Event;
        if (!Guid(P, TEXT("eventId"), R.EventId) || !P->TryGetStringField(TEXT("event"), Event)) return false;
        if (Event == TEXT("tnt_ignite")) R.Kind = EBridgeKind::Tnt;
        else if (Event == TEXT("bow_fire")) {
            R.Kind = EBridgeKind::Bow;
            if (!Vector(P, TEXT("dx"), TEXT("dy"), TEXT("dz"), R.Direction, 1)
                || !Number(P, TEXT("pull"), 0.1, 1, R.Pull)
                || !FMath::IsNearlyEqual(R.Direction.SizeSquared(), 1.0, 0.02)) return false;
        } else if (Event == TEXT("block_preview_clear")) R.Kind = EBridgeKind::ClearPreview;
        else if (Event == TEXT("world_clear")) R.Kind = EBridgeKind::WorldClear;
        else if (Event==TEXT("video_config")) {
            uint64 W,H,F,Q;
            if (!Integer(P,TEXT("width"),160,1920,W) || !Integer(P,TEXT("height"),90,1080,H)
                || !Integer(P,TEXT("fps"),1,30,F) || !Integer(P,TEXT("quality"),30,95,Q)
                || !Number(P,TEXT("exposure"),-6,6,R.VideoExposure)) return false;
            R.Kind=EBridgeKind::VideoConfig; R.VideoWidth=int32(W); R.VideoHeight=int32(H); R.VideoFps=int32(F); R.VideoQuality=int32(Q);
        }
        else if (Event == TEXT("world_scope")) {
            R.Kind = EBridgeKind::WorldScope; double X,Y,Z; uint64 Radius,Height;
            if (!Number(P,TEXT("cellX"),-4000000,4000000,X) || !Number(P,TEXT("cellY"),-4000000,4000000,Y)
                || !Number(P,TEXT("cellZ"),-4000000,4000000,Z) || FMath::FloorToDouble(X)!=X || FMath::FloorToDouble(Y)!=Y || FMath::FloorToDouble(Z)!=Z
                || !Integer(P,TEXT("radius"),1,3,Radius) || !Integer(P,TEXT("halfHeight"),1,2,Height)) return false;
            R.Cell = FIntVector(int32(X),int32(Y),int32(Z)); R.Radius=int32(Radius); R.HalfHeight=int32(Height);
        }
        else if (Event == TEXT("block_snapshot") || Event == TEXT("world_cell") || Event==TEXT("world_cell_textured")) {
            const bool Textured=Event==TEXT("world_cell_textured");
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
                || !Integer(P, TEXT("batchIndex"), 0, (Textured ? 2048 : (WorldCell ? 1024 : MaxBatches)) - 1, Index)
                || !Integer(P, TEXT("totalBatches"), 1, Textured ? 2048 : (WorldCell ? 1024 : MaxBatches), Total) || Index >= Total
                || !P->TryGetArrayField(TEXT("blocks"), Rows) || Rows->Num() > (Textured ? 4 : (WorldCell ? 8 : 12))) return false;
            R.BatchIndex = int32(Index); R.TotalBatches = int32(Total);
            for (const auto& Row : *Rows) {
                const TArray<TSharedPtr<FJsonValue>>* Values;
                const int32 Fields = WorldCell ? 7 : 4;
                if (!Row.IsValid() || !Row->TryGetArray(Values) || Values->Num() != Fields+(Textured ? 1 : 0)) return false;
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
                R.Blocks.Add(FBridgeBlock{FVector(V[0], V[1], V[2]), int32(V[3]), Size, BlockId});
            }
        } else return false;
    } else return false;
    Out = MoveTemp(R); return true;
}
