#include "BridgeNativeInventory.h"

bool UBridgeNativeInventory::EnsureContainer(const FString& Key,const FString& Kind) {
    if(Key.IsEmpty() || (!Stations.Contains(Key) && Stations.Num()>=4096)) return false;
    if(Kind!=TEXT("chest") && Kind!=TEXT("furnace") && Kind!=TEXT("smoker") && Kind!=TEXT("blast_furnace") && Kind!=TEXT("stonecutter") && Kind!=TEXT("hopper") && Kind!=TEXT("dropper") && Kind!=TEXT("dispenser")) return false;
    auto* Existing=Stations.Find(Key);if(Existing) return Existing->Kind==Kind;
    auto& Data=Stations.Add(Key);Data.Kind=Kind;Data.Slots.SetNum(ContainerSize(Kind));Changed();return true;
}
bool UBridgeNativeInventory::ContainerAccepts(const FStation& Data,int32 Slot,const FString& Id,int32 Side) const {
    // Side 1 = top, -1 = bottom, 0 = horizontal. Furnace top is input;
    // horizontal/bottom insertion is fuel; its output never accepts items.
    if(Data.Kind==TEXT("chest") || Data.Kind==TEXT("hopper") || Data.Kind==TEXT("dropper") || Data.Kind==TEXT("dispenser")) return true;
    if(Data.Kind==TEXT("stonecutter")) return false;
    return Side==1 ? Slot==0 : Slot==1 && (Fuels.Contains(Id) || Id==TEXT("minecraft:bucket"));
}
int32 UBridgeNativeInventory::InsertContainer(const FString& Key,const FString& Id,int32 Count,int32 Side) {
    auto* Data=Stations.Find(Key);if(!Data || Count<1 || !ValidItemId(Id)) return Count;const int32 Before=Count;
    for(bool Empty:{false,true}) for(int32 I=0;I<Data->Slots.Num() && Count>0;++I) {
        auto& Stack=Data->Slots[I];if(!ContainerAccepts(*Data,I,Id,Side) || (Empty ? !Stack.IsEmpty() : Stack.IsEmpty() || Stack.ItemId!=Id)) continue;
        const int32 Transfer=FMath::Min(Count,MaxCount(Id)-Stack.Count);if(Transfer>0) {Stack.ItemId=Id;Stack.Count+=Transfer;Count-=Transfer;}
    }
    if(Before!=Count) Changed();return Count;
}
bool UBridgeNativeInventory::TransferContainer(const FString& From,const FString& To,int32 FromSide,int32 ToSide) {
    if(From==To) return false;auto* Source=Stations.Find(From);if(!Source || !Stations.Contains(To)) return false;
    const bool Cooking=Source->Kind==TEXT("furnace") || Source->Kind==TEXT("smoker") || Source->Kind==TEXT("blast_furnace");
    for(int32 I=0;I<Source->Slots.Num();++I) {
        auto& Stack=Source->Slots[I];if(Stack.IsEmpty()) continue;
        if(Cooking && (FromSide!=-1 || (I!=2 && !(I==1 && (Stack.ItemId==TEXT("minecraft:bucket") || Stack.ItemId==TEXT("minecraft:water_bucket")))))) continue;
        if(InsertContainer(To,Stack.ItemId,1,ToSide)==0) {if(--Stack.Count<=0) Stack.Clear();Changed();return true;}
    }
    return false;
}
bool UBridgeNativeInventory::EmitContainerItem(const FString& Key,const TFunction<bool(const FBridgeNativeStack&)>& Spawn) {
    auto* Data=Stations.Find(Key);if(!Data || !Spawn) return false;
    // Vanilla chooses uniformly among occupied slots, rather than among items.
    int32 Selected=-1,Seen=0;for(int32 I=0;I<Data->Slots.Num();++I) if(!Data->Slots[I].IsEmpty() && FMath::RandRange(0,Seen++)==0) Selected=I;
    if(Selected<0) return false;FBridgeNativeStack Item=Data->Slots[Selected];Item.Count=1;
    if(!Spawn(Item)) return false;
    Data=Stations.Find(Key);if(--Data->Slots[Selected].Count<=0) Data->Slots[Selected].Clear();Changed();return true;
}
