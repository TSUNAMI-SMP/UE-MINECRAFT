#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Optional immutable native biome colour field. Never derive render tint from particle colour. */
namespace BridgeBiomeTint {
inline bool Integer(const TSharedPtr<FJsonValue>& Value,int32 High,int32& Out) {
    double Number=0;
    if(!Value.IsValid() || !Value->TryGetNumber(Number) || !FMath::IsFinite(Number) || Number<0 || Number>High || Number!=FMath::FloorToDouble(Number)) return false;
    Out=int32(Number);return true;
}
inline bool Read(const TSharedPtr<FJsonObject>& Cell,TArray<FIntVector>& Out) {
    Out.Empty();if(!Cell.IsValid()) return false;if(!Cell->HasField(TEXT("biomeTints"))) return true;
    const TSharedPtr<FJsonObject>* Field=nullptr;const TArray<TSharedPtr<FJsonValue>> *Palette=nullptr,*Indices=nullptr;
    if(!Cell->TryGetObjectField(TEXT("biomeTints"),Field) || !(*Field)->TryGetArrayField(TEXT("palette"),Palette)
        || Palette->IsEmpty() || Palette->Num()>512 || !(*Field)->TryGetArrayField(TEXT("indices"),Indices) || Indices->Num()!=512) return false;
    TArray<FIntVector> Colors;Colors.Reserve(Palette->Num());
    for(const auto& Value:*Palette) {
        const TArray<TSharedPtr<FJsonValue>>* Row=nullptr;int32 Grass,Foliage,Dry;
        if(!Value.IsValid() || !Value->TryGetArray(Row) || Row->Num()!=3 || !Integer((*Row)[0],0xffffff,Grass)
            || !Integer((*Row)[1],0xffffff,Foliage) || !Integer((*Row)[2],0xffffff,Dry)) return false;
        Colors.Emplace(Grass,Foliage,Dry);
    }
    Out.Reserve(512);
    for(const auto& Value:*Indices) {int32 Index;if(!Integer(Value,Colors.Num()-1,Index)) {Out.Empty();return false;}Out.Add(Colors[Index]);}
    return true;
}
inline TSharedPtr<FJsonObject> Write(const TArray<FIntVector>& Colors) {
    if(Colors.Num()!=512) return nullptr;
    auto Result=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Palette,Indices;TMap<FIntVector,int32> Seen;
    for(const FIntVector& Color:Colors) {
        if(Color.GetMin()<0 || Color.GetMax()>0xffffff) return nullptr;
        const int32* Existing=Seen.Find(Color);int32 Index;
        if(Existing) Index=*Existing;else {
            Index=Palette.Num();Seen.Add(Color,Index);
            Palette.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Color.X),MakeShared<FJsonValueNumber>(Color.Y),MakeShared<FJsonValueNumber>(Color.Z)}));
        }
        Indices.Add(MakeShared<FJsonValueNumber>(Index));
    }
    Result->SetArrayField(TEXT("palette"),Palette);Result->SetArrayField(TEXT("indices"),Indices);return Result;
}
}
