#include "BridgeBlockGeometry.h"
#include "BridgeBlockPalette.h"
#include "BridgeMeshingMath.h"
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
        // UE5.8 JSON keys are TSharedString: operator* returns a NUL-terminated
        // character pointer. This also works with the FString keys used by older UE versions.
        const FString Key(*Pair.Key);
        if(Key==TEXT("OR") || Key==TEXT("AND")) {
            const TArray<TSharedPtr<FJsonValue>>* Terms=nullptr;
            if(!Pair.Value->TryGetArray(Terms) || Terms->IsEmpty()) return false;
            bool Result=Key==TEXT("AND");
            for(const auto& Term:*Terms) {
                const bool Match=MatchWhen(Term->AsObject(),State);
                Result=Key==TEXT("AND") ? Result&&Match : Result||Match;
            }
            if(!Result) return false;
        } else if(!MatchValue(State.Find(Key),Pair.Value->AsString())) return false;
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
        const Object Element=ElementValue->AsObject();FVector From=FVector::ZeroVector,To=FVector::ZeroVector;
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
            double TintIndex=-1;FaceObject->TryGetNumberField(TEXT("tintindex"),TintIndex);Face.bTint=TintIndex>=0;Face.TintIndex=int32(TintIndex);
            FString CullFace;FaceObject->TryGetStringField(TEXT("cullface"),CullFace);
            const FVector CullNormal=CullFace==TEXT("up") ? FVector(0,1,0) : CullFace==TEXT("down") ? FVector(0,-1,0)
                : CullFace==TEXT("east") ? FVector(1,0,0) : CullFace==TEXT("west") ? FVector(-1,0,0)
                : CullFace==TEXT("south") ? FVector(0,0,1) : CullFace==TEXT("north") ? FVector(0,0,-1) : FVector::ZeroVector;
            const FVector Cull=Rotation.RotateVector(CullNormal);
            Face.CullOffset=FIntVector(FMath::RoundToInt(Cull.X),FMath::RoundToInt(Cull.Y),FMath::RoundToInt(Cull.Z));
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
            // Vanilla renders grass's opaque dirt base, then its tinted cutout
            // overlay on the same plane. UE groups material sections and its
            // depth prepass cannot preserve that draw order. Retain both quads
            // and separate only coincident forward-facing layers by 0.05 cm.
            Face.NativeNormal=Rotation.RotateVector(Tilt.RotateVector(Normal)).GetSafeNormal();Face.HasNativeNormal=true;
            const FVector FaceNormal=Face.NativeNormal;
            std::array<BridgeMeshingMath::Point,4> Quad;for(int32 I=0;I<4;++I) Quad[I]={Face.Vertices[I].X,Face.Vertices[I].Y,Face.Vertices[I].Z};
            double LayerDepth=0;
            for(const auto& Previous:Out) {
                std::array<BridgeMeshingMath::Point,4> Other;for(int32 I=0;I<4;++I) Other[I]={Previous.Vertices[I].X,Previous.Vertices[I].Y,Previous.Vertices[I].Z};
                if(BridgeMeshingMath::CoincidentForwardQuads(Quad,Other)) LayerDepth=FMath::Max(LayerDepth,Previous.RenderOffset.Size()+.0005);
            }
            Face.RenderOffset=FaceNormal*LayerDepth;
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
FColor UBridgeBlockPalette::RenderTint(const FString& BlockId,int32 TintIndex) const {
    const Object Tints=Child(ReadShapes(BlockId),TEXT("renderTints"));double RGB=0xffffff;
    if(Tints.IsValid()) Tints->TryGetNumberField(FString::FromInt(TintIndex),RGB);
    if(!FMath::IsFinite(RGB) || RGB<0 || RGB>0xffffff) return FColor::White;
    const uint32 Value=uint32(RGB);return FColor((Value>>16)&255,(Value>>8)&255,Value&255);
}
FString UBridgeBlockPalette::RenderTintSource(const FString& BlockId,int32 TintIndex) const {
    const Object Sources=Child(ReadShapes(BlockId),TEXT("tintSources"));FString Result;
    if(Sources.IsValid()) Sources->TryGetStringField(FString::FromInt(TintIndex),Result);
    return Result;
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
    const FString CacheKey=BlockId+TEXT("[")+StateKey+TEXT("]");
    if(const auto* Cached=CollisionCache.Find(CacheKey)) {Collision=*Cached;Outline=OutlineCache.FindChecked(CacheKey);return true;}
    Collision.Reset();Outline.Reset();const Object Entry=ReadShapes(BlockId);
    const Object States=Child(Entry,TEXT("states"));const Object State=Child(States,StateKey.IsEmpty() ? DefaultState(BlockId) : StateKey);
    const bool Valid=Boxes(State,TEXT("collision"),Collision) && Boxes(State,TEXT("outline"),Outline);
    if(Valid && CollisionCache.Num()<65536) {CollisionCache.Add(CacheKey,Collision);OutlineCache.Add(CacheKey,Outline);}return Valid;
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
bool UBridgeBlockPalette::IsOpaqueFullCube(const FString& BlockId,const FString& StateKey) const {
    const FString Key=BlockId+TEXT("[")+StateKey+TEXT("]");
    if(const bool* Cached=OpaqueCache.Find(Key)) return *Cached;
    const Object State=Child(Child(ReadShapes(BlockId),TEXT("states")),StateKey.IsEmpty() ? DefaultState(BlockId) : StateKey);
    bool Opaque=false;
    // Older manifests never guessed transparency from physics; their faces remain visible.
    if(State.IsValid()) State->TryGetBoolField(TEXT("opaqueFullCube"),Opaque);
    OpaqueCache.Add(Key,Opaque);return Opaque;
}
bool UBridgeBlockPalette::BuildModel(const FString& BlockId,const FString& StateKey,TArray<FBridgeModelFace>& Out) const {
    Out.Reset();const FString State=StateKey.IsEmpty() ? DefaultState(BlockId) : StateKey;
    const FString CacheKey=BlockId+TEXT("[")+State+TEXT("]");
    if(const auto* Cached=ModelCache.Find(CacheKey)) {Out=*Cached;return true;}
    const Object Definition=Read(BlockstateDefinitions.Find(BlockId));if(!Definition.IsValid()) return false;
    const auto StateProperties=Properties(State);TArray<Object> Applications;
    const Object Variants=Child(Definition,TEXT("variants"));
    if(Variants.IsValid()) {
        for(const auto& Pair:Variants->Values) {
            const FString Key(*Pair.Key);
            if(MatchVariant(Key,StateProperties)) {Applications.Add(Choose(Pair.Value));break;}
        }
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

// Item vertices already include vanilla's context-specific model display transform.
bool UBridgeBlockPalette::BuildItem(const FString& ItemId,const FString& Context,TArray<FBridgeModelFace>& Out) const {
    Out.Reset();const FString* Encoded=ItemModels.Find(ItemId);
    if(!Encoded) if(const FString* Default=DefaultItemModels.Find(ItemId)) Encoded=ItemModels.Find(*Default);
    if(!Encoded && !ItemId.Contains(TEXT("@"))) {const FString Prefix=ItemId+TEXT("@");for(const auto& Pair:ItemModels) if(Pair.Key.StartsWith(Prefix)) {Encoded=&Pair.Value;break;}}
    const Object Model=Read(Encoded);
    const TArray<TSharedPtr<FJsonValue>>* Faces=nullptr;
    if(!Model.IsValid() || !Model->TryGetArrayField(Context,Faces) || Faces->IsEmpty() || Faces->Num()>8192) return false;
    for(const auto& Value:*Faces) {
        const Object Data=Value->AsObject();if(!Data) return false;
        const TArray<TSharedPtr<FJsonValue>> *Vertices=nullptr,*UV=nullptr;FBridgeModelFace Face;
        if(!Data->TryGetStringField(TEXT("texture"),Face.TextureId)
            || !Data->TryGetArrayField(TEXT("vertices"),Vertices) || Vertices->Num()!=4 || !Data->TryGetArrayField(TEXT("uv"),UV) || UV->Num()!=4) return false;
        FString AlphaMode=TEXT("masked");Face.DoubleSided=true; // Older item exports used a no-cull master.
        if(Data->HasField(TEXT("doubleSided")) && !Data->TryGetBoolField(TEXT("doubleSided"),Face.DoubleSided)) return false;
        if(Data->HasField(TEXT("alphaMode")) && (!Data->TryGetStringField(TEXT("alphaMode"),AlphaMode)
            || (AlphaMode!=TEXT("masked") && AlphaMode!=TEXT("translucent")))) return false;
        if(AlphaMode==TEXT("translucent")) Face.TextureId+=TEXT("#translucent");
        if(!ItemMaterials.Contains(Face.TextureId)) return false;
        if(Data->HasField(TEXT("normal"))) {
            const TArray<TSharedPtr<FJsonValue>>* N=nullptr;double Components[3];
            if(!Data->TryGetArrayField(TEXT("normal"),N) || N->Num()!=3) return false;
            for(int32 I=0;I<3;++I) if(!(*N)[I]->TryGetNumber(Components[I]) || !FMath::IsFinite(Components[I]) || FMath::Abs(Components[I])>4096) return false;
            Face.NativeNormal=FVector(Components[0],Components[1],Components[2]).GetSafeNormal();
            Face.HasNativeNormal=!Face.NativeNormal.IsNearlyZero();
        }
        double Color=0xffffff;if(!Data->TryGetNumberField(TEXT("color"),Color) || Color<0 || Color>0xffffff || Color!=double(FMath::FloorToInt(Color))) return false;
        Face.Color=FColor((int32(Color)>>16)&255,(int32(Color)>>8)&255,int32(Color)&255);
        for(int32 I=0;I<4;++I) {
            const TArray<TSharedPtr<FJsonValue>> *P=nullptr,*T=nullptr;
            if(!(*Vertices)[I]->TryGetArray(P) || P->Num()!=3 || !(*UV)[I]->TryGetArray(T) || T->Num()!=2) return false;
            double C[5];for(int32 J=0;J<5;++J) if(!(J<3 ? (*P)[J] : (*T)[J-3])->TryGetNumber(C[J]) || !FMath::IsFinite(C[J]) || FMath::Abs(C[J])>4096) return false;
            Face.Vertices[I]=FVector(C[0],C[1],C[2]);Face.UV[I]=FVector2D(C[3],C[4]);
        }
        // Preserve opaque base + tinted grass overlay even when UE batches sections.
        const FVector N=Face.HasNativeNormal ? Face.NativeNormal : FVector::CrossProduct(Face.Vertices[1]-Face.Vertices[0],Face.Vertices[2]-Face.Vertices[0]).GetSafeNormal();
        std::array<BridgeMeshingMath::Point,4> Q;for(int32 I=0;I<4;++I) Q[I]={Face.Vertices[I].X,Face.Vertices[I].Y,Face.Vertices[I].Z};
        for(const auto& Previous:Out) {std::array<BridgeMeshingMath::Point,4> P;for(int32 I=0;I<4;++I) P[I]={Previous.Vertices[I].X,Previous.Vertices[I].Y,Previous.Vertices[I].Z};
            if(BridgeMeshingMath::CoincidentForwardQuads(Q,P)) Face.RenderOffset=N*FMath::Max(Face.RenderOffset.Size(),Previous.RenderOffset.Size()+.0005);}
        Out.Add(MoveTemp(Face));
    }
    return !Out.IsEmpty();
}
