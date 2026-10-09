#include "BridgeNativeInventory.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UBridgeNativeInventory::LoadGameplay() {
    Recipes.clear();Fuels.Empty();Remainders.Empty();Stations.Empty();Station.Reset();StationKey.Reset();StationClock=0;
    TSharedPtr<FJsonObject> Data;
    if(!Palette || Palette->GameplayData.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Palette->GameplayData),Data) || !Data.IsValid()) return;
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
    if(Data->TryGetArrayField(TEXT("recipes"),Entries) && Entries->Num()<=32768) for(const auto& Value:*Entries) {
        const TSharedPtr<FJsonObject>* Row=nullptr;FString Id,Type,Result;double Count=0;
        if(!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("result"),Result) || !(*Row)->TryGetStringField(TEXT("id"),Id)
            || !(*Row)->TryGetStringField(TEXT("type"),Type) || !(*Row)->TryGetNumberField(TEXT("count"),Count) || Count<1 || Count>99) continue;
        if(Palette && !Palette->FindItem(Result)) continue;
        BridgeRecipeMath::Recipe Recipe;Recipe.id=TCHAR_TO_UTF8(*Id);Recipe.type=TCHAR_TO_UTF8(*Type);Recipe.result=TCHAR_TO_UTF8(*Result);Recipe.count=int32(Count);
        double N=0;if((*Row)->TryGetNumberField(TEXT("width"),N)) Recipe.width=int32(N);if((*Row)->TryGetNumberField(TEXT("height"),N)) Recipe.height=int32(N);if((*Row)->TryGetNumberField(TEXT("ticks"),N)) Recipe.ticks=FMath::Max(1,int32(N));
        const TArray<TSharedPtr<FJsonValue>>* Inputs=nullptr;if(!(*Row)->TryGetArrayField(TEXT("ingredients"),Inputs) || Inputs->Num()>9) continue;
        bool Valid=true;for(const auto& Input:*Inputs) {const TArray<TSharedPtr<FJsonValue>>* Alternatives=nullptr;if(!Input->TryGetArray(Alternatives) || Alternatives->Num()>8192) {Valid=false;break;}
            BridgeRecipeMath::Ingredient Ingredient;for(const auto& Alternative:*Alternatives) {FString Item;if(!Alternative->TryGetString(Item)) {Valid=false;break;}Ingredient.emplace_back(TCHAR_TO_UTF8(*Item));}Recipe.ingredients.push_back(MoveTemp(Ingredient));}
        if(Valid) Recipes.push_back(MoveTemp(Recipe));
    }
    const TSharedPtr<FJsonObject>* Table=nullptr;
    if(Data->TryGetObjectField(TEXT("fuels"),Table)) for(const auto& Pair:(*Table)->Values) {double Ticks=0;if(Pair.Value->TryGetNumber(Ticks) && Ticks>0 && Ticks<=1000000) Fuels.Add(FString(*Pair.Key),int32(Ticks));}
    if(Data->TryGetObjectField(TEXT("remainders"),Table)) for(const auto& Pair:(*Table)->Values) {FString Id;if(Pair.Value->TryGetString(Id)) Remainders.Add(FString(*Pair.Key),Id);}
    const TArray<TSharedPtr<FJsonValue>>* Containers=nullptr;
    if(Data->TryGetArrayField(TEXT("containers"),Containers) && Containers->Num()<=4096) for(const auto& Value:*Containers) {
        const TSharedPtr<FJsonObject>* Row=nullptr;FString Key,Kind;const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
        if(!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("key"),Key) || !(*Row)->TryGetStringField(TEXT("kind"),Kind) || !(*Row)->TryGetArrayField(TEXT("slots"),Items) || Items->Num()!=ContainerSize(Kind)) continue;
        FStation Container;Container.Kind=Kind;Container.Slots.SetNum(Items->Num());bool Valid=true;
        for(int32 I=0;I<Items->Num();++I) {const TSharedPtr<FJsonObject>* Item=nullptr;FString Id;double Count=0;
            if(!(*Items)[I]->TryGetObject(Item) || !(*Item)->TryGetStringField(TEXT("item"),Id) || !(*Item)->TryGetNumberField(TEXT("count"),Count) || Count<0 || Count>MaxCount(Id)) {Valid=false;break;}
            if(Count>0) {Container.Slots[I].ItemId=Id;Container.Slots[I].Count=int32(Count);}
        }
        double N=0;if((*Row)->TryGetNumberField(TEXT("burn"),N)) Container.Burn=int32(N);if((*Row)->TryGetNumberField(TEXT("burnTotal"),N)) Container.BurnTotal=int32(N);if((*Row)->TryGetNumberField(TEXT("cook"),N)) Container.Cook=int32(N);
        if(Valid) Stations.Add(Key,MoveTemp(Container));
    }
}
const BridgeRecipeMath::Recipe* UBridgeNativeInventory::MatchingCraft() const {
    std::vector<std::string> Grid;const int32 W=GetCraftWidth();
    for(int32 Y=0;Y<W;++Y) for(int32 X=0;X<W;++X) {const auto& Stack=CraftGrid[X+Y*3];Grid.emplace_back(Stack.IsEmpty() ? "" : TCHAR_TO_UTF8(*Stack.ItemId));}
    for(const auto& Recipe:Recipes) if(BridgeRecipeMath::Matches(Recipe,Grid,W,W)) return &Recipe;return nullptr;
}
FBridgeNativeStack UBridgeNativeInventory::CraftResult() const {
    FBridgeNativeStack Result;if(const auto* Recipe=MatchingCraft()) {Result.ItemId=UTF8_TO_TCHAR(Recipe->result.c_str());Result.Count=Recipe->count;}return Result;
}
bool UBridgeNativeInventory::TakeCraftResult(bool Shift) {
    bool Crafted=false;
    for(int32 Attempt=0;Attempt<(Shift ? 99 : 1);++Attempt) {
        const auto* Recipe=MatchingCraft();if(!Recipe) break;
        const FString Result=UTF8_TO_TCHAR(Recipe->result.c_str());const int32 Count=Recipe->count;
        if(!Shift && (!CursorStack.IsEmpty() && CursorStack.ItemId!=Result || CursorStack.Count+Count>MaxCount(Result))) break;
        const auto BeforeSlots=Slots;const auto BeforeGrid=CraftGrid;const auto BeforeCursor=CursorStack;
        bool Space=true;
        if(Shift) Space=InsertRange(Result,Count,0,36)==0;
        else {CursorStack.ItemId=Result;CursorStack.Count+=Count;}
        if(Space) for(int32 I=0;I<9;++I) if(I%3<GetCraftWidth() && I/3<GetCraftWidth() && !CraftGrid[I].IsEmpty()) {
            auto& Stack=CraftGrid[I];
            const FString Remainder=Remainders.FindRef(Stack.ItemId);--Stack.Count;if(!Stack.Count) Stack.Clear();
            if(!Remainder.IsEmpty()) {if(Stack.IsEmpty()) {Stack.ItemId=Remainder;Stack.Count=1;}else if(InsertRange(Remainder,1,0,36)!=0) {Space=false;break;}}
        }
        if(!Space) {Slots=BeforeSlots;CraftGrid=BeforeGrid;CursorStack=BeforeCursor;break;}
        Crafted=true;Changed();
    }
    return Crafted;
}
bool UBridgeNativeInventory::OpenStation(const FString& Kind,const FString& Key) {
    if(Kind!=TEXT("crafting_table") && Kind!=TEXT("furnace") && Kind!=TEXT("blast_furnace") && Kind!=TEXT("smoker") && Kind!=TEXT("stonecutter") && Kind!=TEXT("chest") && Kind!=TEXT("hopper") && Kind!=TEXT("dropper") && Kind!=TEXT("dispenser")) return false;
    if(Key.IsEmpty() || (Kind!=TEXT("crafting_table") && !Stations.Contains(Key) && Stations.Num()>=4096)) return false;
    if(Kind!=TEXT("crafting_table") && !EnsureContainer(Key,Kind)) return false;
    CloseStation();Station=Kind;StationKey=Key;StonecuttingIndex=0;
    return true;
}
void UBridgeNativeInventory::CloseStation() {
    // Return grid contents without deleting overflow. A full inventory retains
    // remaining input in the persistent personal grid until space becomes free.
    for(auto& Stack:CraftGrid) if(!Stack.IsEmpty()) {Stack.Count=InsertRange(Stack.ItemId,Stack.Count,0,36);if(!Stack.Count) Stack.Clear();}
    Station.Reset();StationKey.Reset();Changed();
}
const BridgeRecipeMath::Recipe* UBridgeNativeInventory::MatchingSingle(const FStation& Data) const {
    if(Data.Slots.IsEmpty() || Data.Slots[0].IsEmpty()) return nullptr;
    const std::string Type=Data.Kind==TEXT("stonecutter") ? "minecraft:stonecutting" : Data.Kind==TEXT("smoker") ? "minecraft:smoking" : Data.Kind==TEXT("blast_furnace") ? "minecraft:blasting" : "minecraft:smelting";
    int32 Index=0;for(const auto& Recipe:Recipes) if(BridgeRecipeMath::SingleMatches(Recipe,TCHAR_TO_UTF8(*Data.Slots[0].ItemId),Type)) {if(Data.Kind!=TEXT("stonecutter") || Index++==StonecuttingIndex) return &Recipe;}return nullptr;
}
TArray<FString> UBridgeNativeInventory::StonecuttingResults() const {
    TArray<FString> Results;const auto* Data=Stations.Find(StationKey);if(!Data || Data->Slots.IsEmpty()) return Results;
    for(const auto& Recipe:Recipes) if(BridgeRecipeMath::SingleMatches(Recipe,TCHAR_TO_UTF8(*Data->Slots[0].ItemId),"minecraft:stonecutting")) Results.Add(UTF8_TO_TCHAR(Recipe.result.c_str()));return Results;
}
FBridgeNativeStack UBridgeNativeInventory::StationResult() const {
    FBridgeNativeStack Result;const auto* Data=Stations.Find(StationKey);if(!Data) return Result;
    if(Data->Kind==TEXT("stonecutter")) {if(const auto* Recipe=MatchingSingle(*Data)) {Result.ItemId=UTF8_TO_TCHAR(Recipe->result.c_str());Result.Count=Recipe->count;}}
    else if(Data->Slots.IsValidIndex(2)) Result=Data->Slots[2];return Result;
}
bool UBridgeNativeInventory::TakeStationResult(bool Shift) {
    auto* Data=Stations.Find(StationKey);if(!Data) return false;bool Taken=false;
    for(int32 Attempt=0;Attempt<(Shift && Data->Kind==TEXT("stonecutter") ? 99 : 1);++Attempt) {
        const auto Result=StationResult();if(Result.IsEmpty()) break;
        if(Shift) {const auto Before=Slots;if(InsertRange(Result.ItemId,Result.Count,0,36)!=0) {Slots=Before;break;}}
        else {if((!CursorStack.IsEmpty() && CursorStack.ItemId!=Result.ItemId) || CursorStack.Count+Result.Count>MaxCount(Result.ItemId)) break;
            CursorStack.ItemId=Result.ItemId;CursorStack.Count+=Result.Count;}
        if(Data->Kind==TEXT("stonecutter")) {if(--Data->Slots[0].Count<=0) Data->Slots[0].Clear();}else Data->Slots[2].Clear();Taken=true;Changed();
    }
    return Taken;
}
void UBridgeNativeInventory::TickStations(float DeltaSeconds) {
    StationClock+=FMath::Max(0.f,DeltaSeconds);int32 Steps=0;
    while(StationClock>=.05f && ++Steps<=20) {StationClock-=.05f;
        for(auto& Pair:Stations) {auto& Data=Pair.Value;if(Data.Kind!=TEXT("furnace") && Data.Kind!=TEXT("smoker") && Data.Kind!=TEXT("blast_furnace")) continue;
            const bool WasLit=Data.Burn>0;
            const auto* Recipe=MatchingSingle(Data);const FString RecipeId=Recipe ? FString(UTF8_TO_TCHAR(Recipe->id.c_str())) : FString();
            if(!Data.RecipeId.IsEmpty() && Data.RecipeId!=RecipeId) Data.Cook=0;Data.RecipeId=RecipeId;
            if(Data.Burn>0) --Data.Burn;
            const FString Result=Recipe ? FString(UTF8_TO_TCHAR(Recipe->result.c_str())) : FString();
            const bool CanCook=Recipe && (Data.Slots[2].IsEmpty() || Data.Slots[2].ItemId==Result) && Data.Slots[2].Count+Recipe->count<=MaxCount(Result);
            if(!Data.Burn && CanCook && !Data.Slots[1].IsEmpty()) {
                const int32 Fuel=Fuels.FindRef(Data.Slots[1].ItemId);if(Fuel>0) {
                    const FString Remainder=Remainders.FindRef(Data.Slots[1].ItemId);Data.Burn=Data.BurnTotal=Fuel;
                    if(--Data.Slots[1].Count<=0) {Data.Slots[1].Clear();if(!Remainder.IsEmpty()) {Data.Slots[1].ItemId=Remainder;Data.Slots[1].Count=1;}}Changed();
                }
            }
            if(Data.Burn && CanCook) {if(++Data.Cook>=Recipe->ticks) {Data.Cook=0;if(--Data.Slots[0].Count<=0) Data.Slots[0].Clear();Data.Slots[2].ItemId=Result;Data.Slots[2].Count+=Recipe->count;Changed();}}
            else if(!CanCook) Data.Cook=0;else Data.Cook=FMath::Max(0,Data.Cook-2);
            if(WasLit!=(Data.Burn>0) && FurnaceLitChanged) FurnaceLitChanged(Pair.Key,Data.Burn>0);
        }
    }
}
float UBridgeNativeInventory::CookingProgress() const {const auto* Data=Stations.Find(StationKey);const auto* Recipe=Data ? MatchingSingle(*Data) : nullptr;return Recipe ? float(Data->Cook)/Recipe->ticks : 0;}
float UBridgeNativeInventory::FuelProgress() const {const auto* Data=Stations.Find(StationKey);return Data && Data->BurnTotal>0 ? float(Data->Burn)/Data->BurnTotal : 0;}
bool UBridgeNativeInventory::EquipSelected() {
    const int32 Slot=EquipmentSlotFor(Selected().ItemId);auto* Armor=MutableStack(Slot);if(!Armor || Selected().IsEmpty()) return false;
    if(Armor->IsEmpty()) {Armor->ItemId=Selected().ItemId;Armor->Count=1;return ConsumeSelected(1);}
    if(Selected().Count!=1) return false;Swap(*Armor,Slots[SelectedSlot]);Changed();return true;
}

int32 UBridgeNativeInventory::ContainerSignal(const FString& Key) const {
    const auto* Data=Stations.Find(Key);if(!Data || Data->Slots.IsEmpty() || Data->Kind==TEXT("stonecutter")) return 0;
    float Fullness=0;bool Any=false;for(const auto& Stack:Data->Slots) if(!Stack.IsEmpty()) {Fullness+=float(Stack.Count)/MaxCount(Stack.ItemId);Any=true;}
    return FMath::FloorToInt(Fullness/Data->Slots.Num()*14)+(Any ? 1 : 0);
}
TArray<FBridgeNativeStack> UBridgeNativeInventory::ContainerContents(const FString& Key) const {
    const auto* Data=Stations.Find(Key);return Data ? Data->Slots : TArray<FBridgeNativeStack>();
}
void UBridgeNativeInventory::RemoveContainer(const FString& Key) {
    if(StationKey==Key) CloseStation();
    if(Stations.Remove(Key)) Changed();
}
