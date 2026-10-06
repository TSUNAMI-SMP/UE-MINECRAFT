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
        else if (Event == TEXT("block_snapshot")) {
            R.Kind = EBridgeKind::Snapshot; uint64 Index, Total;
            const TArray<TSharedPtr<FJsonValue>>* Rows;
            if (!Guid(P, TEXT("snapshotId"), R.SnapshotId)
                || !Integer(P, TEXT("snapshotSeq"), 1, 9007199254740991.0, R.SnapshotSequence)
                || R.SnapshotSequence > R.Sequence
                || !Integer(P, TEXT("batchIndex"), 0, MaxBatches - 1, Index)
                || !Integer(P, TEXT("totalBatches"), 1, MaxBatches, Total) || Index >= Total
                || !P->TryGetArrayField(TEXT("blocks"), Rows) || Rows->Num() > 12) return false;
            R.BatchIndex = int32(Index); R.TotalBatches = int32(Total);
            for (const auto& Row : *Rows) {
                const TArray<TSharedPtr<FJsonValue>>* Values;
                if (!Row.IsValid() || !Row->TryGetArray(Values) || Values->Num() != 4) return false;
                double V[4];
                for (int32 I = 0; I < 4; ++I) if (!(*Values)[I].IsValid() || (*Values)[I]->Type != EJson::Number
                    || !(*Values)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I])) return false;
                if (FMath::Abs(V[0]) > 100000 || FMath::Abs(V[1]) > 100000 || FMath::Abs(V[2]) > 100000
                    || V[3] < 0 || V[3] > 0xffffff || FMath::FloorToDouble(V[3]) != V[3]) return false;
                R.Blocks.Add(FBridgeBlock{FVector(V[0], V[1], V[2]), int32(V[3])});
            }
        } else return false;
    } else return false;
    Out = MoveTemp(R); return true;
}
