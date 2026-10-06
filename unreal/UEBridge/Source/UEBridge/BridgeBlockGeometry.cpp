#include "BridgeBlockPalette.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
using Object=TSharedPtr<FJsonObject>;
Object Read(const FString* Text) {
    Object Result;
    if(Text) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),Result);
    return Result;
}
Object Child(const Object& Parent,const FString& Key) {
    const Object* Value=nullptr;
    return Parent.IsValid() && Parent->TryGetObjectField(Key,Value) ? *Value : nullptr;
}
bool Vector(const Object& Parent,const FString& Key,FVector& Out) {
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Parent.IsValid() || !Parent->TryGetArrayField(Key,Values) || Values->Num()!=3) return false;
    Out=FVector((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber(),(*Values)[2]->AsNumber());return true;
}
TMap<FString,FString> Properties(const FString& Key) {
    TMap<FString,FString> Result;TArray<FString> Parts;Key.ParseIntoArray(Parts,TEXT(","));
    for(const auto& Part:Parts) {FString Name,Value;if(Part.Split(TEXT("="),&Name,&Value)) Result.Add(Name,Value);}
    return Result;
}
bool MatchValue(const FString* Actual,const FString& Expected) {
    if(!Actual) return false;
    TArray<FString> Options;Expected.ParseIntoArray(Options,TEXT("|"));return Options.Contains(*Actual);
}
bool MatchVariant(const FString& Key,const TMap<FString,FString>& State) {
    for(const auto& Pair:Properties(Key)) if(!MatchValue(State.Find(Pair.Key),Pair.Value)) return false;
    return true;
}
bool MatchWhen(const Object& When,const TMap<FString,FString>& State) {
    if(!When.IsValid()) return true;
    for(const auto& Pair:When->Values) {
        if(Pair.Key==TEXT("OR") || Pair.Key==TEXT("AND")) {
            const TArray<TSharedPtr<FJsonValue>>* Terms=nullptr;
            if(!Pair.Value->TryGetArray(Terms) || Terms->IsEmpty()) return false;
            bool Result=Pair.Key==TEXT("AND");
            for(const auto& Term:*Terms) {
                const bool Match=MatchWhen(Term->AsObject(),State);
                Result=Pair.Key==TEXT("AND") ? Result&&Match : Result||Match;
            }
            if(!Result) return false;
        } else if(!MatchValue(State.Find(Pair.Key),Pair.Value->AsString())) return false;
    }
    return true;
}
Object Choose(const TSharedPtr<FJsonValue>& Value) {
    if(!Value) return nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Options=nullptr;
    // Minecraft's randomly weighted alternatives share gameplay geometry. A fixed first
    // choice makes imports deterministic and avoids geometry flickering on cell refresh.
    if(Value->TryGetArray(Options)) return Options->IsEmpty() ? nullptr : (*Options)[0]->AsObject();
    return Value->AsObject();
}
void Basis(const FVector& Normal,FVector& U,FVector& V) {
    const FVector N=Normal.GetAbs();
    if(N.Y>=N.X && N.Y>=N.Z) {U=FVector(1,0,0);V=FVector(0,0,Normal.Y>0 ? 1 : -1);}
    else if(N.Z>=N.X) {U=FVector(Normal.Z>0 ? 1 : -1,0,0);V=FVector(0,-1,0);}
    else {U=FVector(0,0,Normal.X>0 ? -1 : 1);V=FVector(0,-1,0);}
}
bool Bake(const Object& Model,const Object& Application,TArray<FBridgeModelFace>& Out) {
    const TArray<TSharedPtr<FJsonValue>>* Elements=nullptr;
    if(!Model.IsValid() || !Model->TryGetArrayField(TEXT("elements"),Elements)) return false;
    double X=0,Y=0;Application->TryGetNumberField(TEXT("x"),X);Application->TryGetNumberField(TEXT("y"),Y);
    const FQuat Rotation=FQuat(FVector(0,1,0),FMath::DegreesToRadians(-Y))*FQuat(FVector(1,0,0),FMath::DegreesToRadians(-X));
    bool UVLock=false;Application->TryGetBoolField(TEXT("uvlock"),UVLock);
    for(const auto& ElementValue:*Elements) {
        const Object Element=ElementValue->AsObject();FVector From,To;
        if(!Vector(Element,TEXT("from"),From) || !Vector(Element,TEXT("to"),To)) return false;
        const Object Faces=Child(Element,TEXT("faces"));if(!Faces.IsValid()) return false;
        const Object ElementRotation=Child(Element,TEXT("rotation"));
        FVector Origin(8,8,8),Scale(1);FQuat Tilt=FQuat::Identity;
        if(ElementRotation.IsValid()) {
            Vector(ElementRotation,TEXT("origin"),Origin);
            FString Axis;ElementRotation->TryGetStringField(TEXT("axis"),Axis);
            double Angle=0;ElementRotation->TryGetNumberField(TEXT("angle"),Angle);
            const FVector Direction=Axis==TEXT("x") ? FVector(1,0,0) : Axis==TEXT("y") ? FVector(0,1,0) : FVector(0,0,1);
            Tilt=FQuat(Direction,FMath::DegreesToRadians(Angle));
            bool Rescale=false;ElementRotation->TryGetBoolField(TEXT("rescale"),Rescale);
            if(Rescale) {const double Factor=1/FMath::Cos(FMath::DegreesToRadians(Angle));Scale=FVector(Factor)-Direction*(Factor-1);}
        }
        for(const auto& Pair:Faces->Values) {
            const Object FaceObject=Pair.Value->AsObject();if(!FaceObject.IsValid()) return false;
            FBridgeModelFace Face;
            if(!FaceObject->TryGetStringField(TEXT("texture"),Face.TextureId)) return false;
            double TintIndex=-1;FaceObject->TryGetNumberField(TEXT("tintindex"),TintIndex);Face.bTint=TintIndex>=0;
            FVector Normal;FVector4 Rectangle;
            const double A=From.X,B=From.Y,C=From.Z,D=To.X,E=To.Y,F=To.Z;
            if(Pair.Key==TEXT("down")) {
                Face.Vertices[0]=FVector(A,B,F);Face.Vertices[1]=FVector(A,B,C);Face.Vertices[2]=FVector(D,B,C);Face.Vertices[3]=FVector(D,B,F);
                Normal=FVector(0,-1,0);Rectangle=FVector4(A,16-F,D,16-C);
            } else if(Pair.Key==TEXT("up")) {
                Face.Vertices[0]=FVector(A,E,C);Face.Vertices[1]=FVector(A,E,F);Face.Vertices[2]=FVector(D,E,F);Face.Vertices[3]=FVector(D,E,C);
                Normal=FVector(0,1,0);Rectangle=FVector4(A,C,D,F);
            } else if(Pair.Key==TEXT("north")) {
                Face.Vertices[0]=FVector(D,E,C);Face.Vertices[1]=FVector(D,B,C);Face.Vertices[2]=FVector(A,B,C);Face.Vertices[3]=FVector(A,E,C);
                Normal=FVector(0,0,-1);Rectangle=FVector4(16-D,16-E,16-A,16-B);
            } else if(Pair.Key==TEXT("south")) {
                Face.Vertices[0]=FVector(A,E,F);Face.Vertices[1]=FVector(A,B,F);Face.Vertices[2]=FVector(D,B,F);Face.Vertices[3]=FVector(D,E,F);
                Normal=FVector(0,0,1);Rectangle=FVector4(A,16-E,D,16-B);
            } else if(Pair.Key==TEXT("west")) {
                Face.Vertices[0]=FVector(A,E,C);Face.Vertices[1]=FVector(A,B,C);Face.Vertices[2]=FVector(A,B,F);Face.Vertices[3]=FVector(A,E,F);
                Normal=FVector(-1,0,0);Rectangle=FVector4(C,16-E,F,16-B);
            } else if(Pair.Key==TEXT("east")) {
                Face.Vertices[0]=FVector(D,E,F);Face.Vertices[1]=FVector(D,B,F);Face.Vertices[2]=FVector(D,B,C);Face.Vertices[3]=FVector(D,E,C);
                Normal=FVector(1,0,0);Rectangle=FVector4(16-F,16-E,16-C,16-B);
            } else return false;
            const TArray<TSharedPtr<FJsonValue>>* UV=nullptr;
            if(FaceObject->TryGetArrayField(TEXT("uv"),UV) && UV->Num()==4) Rectangle=FVector4((*UV)[0]->AsNumber(),(*UV)[1]->AsNumber(),(*UV)[2]->AsNumber(),(*UV)[3]->AsNumber());
            const FVector2D Corners[4]={FVector2D(Rectangle.X,Rectangle.Y)/16,FVector2D(Rectangle.X,Rectangle.W)/16,FVector2D(Rectangle.Z,Rectangle.W)/16,FVector2D(Rectangle.Z,Rectangle.Y)/16};
            double UVRotation=0;FaceObject->TryGetNumberField(TEXT("rotation"),UVRotation);
            FVector OldU,OldV,NewU,NewV;Basis(Normal,OldU,OldV);Basis(Rotation.RotateVector(Normal),NewU,NewV);
            for(int32 I=0;I<4;++I) {
                Face.Vertices[I]=Rotation.RotateVector((Origin+Tilt.RotateVector(Face.Vertices[I]-Origin)*Scale-FVector(8))/16)+FVector(.5);
                Face.UV[I]=Corners[(I+static_cast<int32>(UVRotation)/90)%4];
                if(UVLock) {
                    const FVector2D Delta=Face.UV[I]-FVector2D(.5,.5);
                    const FVector BasisVector=Rotation.RotateVector(OldU)*Delta.X+Rotation.RotateVector(OldV)*Delta.Y;
                    Face.UV[I]=FVector2D(.5+FVector::DotProduct(BasisVector,NewU),.5+FVector::DotProduct(BasisVector,NewV));
                }
            }
            Out.Add(MoveTemp(Face));if(Out.Num()>4096) return false;
        }
    }
    return true;
}
bool Boxes(const Object& State,const FString& Key,TArray<FBox>& Out) {
    const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
    if(!State.IsValid() || !State->TryGetArrayField(Key,List)) return false;
    for(const auto& Item:*List) {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!Item->TryGetArray(Values) || Values->Num()!=6) return false;
        Out.Emplace(FVector((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber(),(*Values)[2]->AsNumber()),FVector((*Values)[3]->AsNumber(),(*Values)[4]->AsNumber(),(*Values)[5]->AsNumber()));
    }
    return true;
}
}

TSharedPtr<FJsonObject> UBridgeBlockPalette::ReadShapes(const FString& BlockId) const {
    if(const auto* Cached=ShapeCache.Find(BlockId)) return *Cached;
    const Object Result=Read(StateShapes.Find(BlockId));ShapeCache.Add(BlockId,Result);return Result;
}
FString UBridgeBlockPalette::DefaultState(const FString& BlockId) const {
    const Object Entry=ReadShapes(BlockId);FString Result;
    if(Entry.IsValid()) Entry->TryGetStringField(TEXT("defaultState"),Result);return Result;
}
FVector UBridgeBlockPalette::GetModelOffset(const FString& BlockId,const FIntVector& SourceBlock) const {
    const Object Entry=ReadShapes(BlockId);const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Entry.IsValid() || !Entry->TryGetArrayField(TEXT("modelOffset"),Values) || Values->Num()!=2) return FVector::ZeroVector;
    const double Horizontal=(*Values)[0]->AsNumber(),Vertical=(*Values)[1]->AsNumber();
    if(Horizontal==0 && Vertical==0) return FVector::ZeroVector;
    // Match Java's int overflow before widening x, then long overflow, including
    // the float division used by native AbstractBlock.Settings' offset function.
    const uint32 Product=uint32(SourceBlock.X)*3129871u;
    const int64 SignedX=Product&0x80000000u ? int64(Product)-4294967296LL : int64(Product);
    uint64 Hash=uint64(SignedX)^(uint64(int64(SourceBlock.Z))*116129781ULL);
    Hash=Hash*Hash*42317861ULL+Hash*11ULL;
    const double X=(double(float((Hash>>16)&15)/15.f)-.5)*.5;
    const double Z=(double(float((Hash>>24)&15)/15.f)-.5)*.5;
    const double Y=(double(float((Hash>>20)&15)/15.f)-1)*Vertical;
    return FVector(FMath::Clamp(X,-Horizontal,Horizontal),Y,FMath::Clamp(Z,-Horizontal,Horizontal));
}
bool UBridgeBlockPalette::GetStateBoxes(const FString& BlockId,const FString& StateKey,TArray<FBox>& Collision,TArray<FBox>& Outline) const {
    Collision.Reset();Outline.Reset();const Object Entry=ReadShapes(BlockId);
    const Object States=Child(Entry,TEXT("states"));const Object State=Child(States,StateKey.IsEmpty() ? DefaultState(BlockId) : StateKey);
    return Boxes(State,TEXT("collision"),Collision) && Boxes(State,TEXT("outline"),Outline);
}
bool UBridgeBlockPalette::HasSolidFace(const FString& BlockId,const FString& StateKey,const FString& Face) const {
    const Object State=Child(Child(ReadShapes(BlockId),TEXT("states")),StateKey.IsEmpty() ? DefaultState(BlockId) : StateKey);
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(State.IsValid() && State->TryGetArrayField(TEXT("solidFaces"),Values)) {
        for(const auto& Value:*Values) if(Value->AsString()==Face) return true;return false;
    }
    TArray<FBox> A,B;if(!GetStateBoxes(BlockId,StateKey,A,B)) return false;
    for(const auto& Box:A) if(Box.Min.X<=0 && Box.Min.Y<=0 && Box.Min.Z<=0 && Box.Max.X>=1 && Box.Max.Y>=1 && Box.Max.Z>=1) return true;
    return false;
}
bool UBridgeBlockPalette::CannotConnect(const FString& BlockId,const FString& StateKey) const {
    const Object State=Child(Child(ReadShapes(BlockId),TEXT("states")),StateKey.IsEmpty() ? DefaultState(BlockId) : StateKey);
    bool Result=false;if(State.IsValid()) State->TryGetBoolField(TEXT("cannotConnect"),Result);return Result;
}
bool UBridgeBlockPalette::BuildModel(const FString& BlockId,const FString& StateKey,TArray<FBridgeModelFace>& Out) const {
    Out.Reset();const FString State=StateKey.IsEmpty() ? DefaultState(BlockId) : StateKey;
    const FString CacheKey=BlockId+TEXT("[")+State+TEXT("]");
    if(const auto* Cached=ModelCache.Find(CacheKey)) {Out=*Cached;return true;}
    const Object Definition=Read(BlockstateDefinitions.Find(BlockId));if(!Definition.IsValid()) return false;
    const auto StateProperties=Properties(State);TArray<Object> Applications;
    const Object Variants=Child(Definition,TEXT("variants"));
    if(Variants.IsValid()) {
        for(const auto& Pair:Variants->Values) if(MatchVariant(Pair.Key,StateProperties)) {Applications.Add(Choose(Pair.Value));break;}
    }
    const TArray<TSharedPtr<FJsonValue>>* Multipart=nullptr;
    if(Definition->TryGetArrayField(TEXT("multipart"),Multipart)) for(const auto& Part:*Multipart) {
        const Object Entry=Part->AsObject();
        if(Entry.IsValid() && MatchWhen(Child(Entry,TEXT("when")),StateProperties)) Applications.Add(Choose(Entry->TryGetField(TEXT("apply"))));
    }
    if(Applications.IsEmpty()) return Multipart!=nullptr;
    for(const auto& Application:Applications) {
        FString ModelId;if(!Application.IsValid() || !Application->TryGetStringField(TEXT("model"),ModelId)) {Out.Reset();return false;}
        if(!ModelId.Contains(TEXT(":"))) ModelId=TEXT("minecraft:")+ModelId;
        if(!Bake(Read(Models.Find(ModelId)),Application,Out)) {Out.Reset();return false;}
    }
    if(ModelCache.Num()<65536) ModelCache.Add(CacheKey,Out);return true;
}
