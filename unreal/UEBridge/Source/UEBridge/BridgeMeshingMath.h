#pragma once
#include <array>
#include <vector>

/** Production 8³ collider packing. Boxes contain exactly the occupied native voxels. */
namespace BridgeMeshingMath {
using Point=std::array<double,3>;
inline Point Cross(const Point& A,const Point& B) {return {A[1]*B[2]-A[2]*B[1],A[2]*B[0]-A[0]*B[2],A[0]*B[1]-A[1]*B[0]};}
inline Point Subtract(const Point& A,const Point& B) {return {A[0]-B[0],A[1]-B[1],A[2]-B[2]};}
inline Point MCToUE(const Point& P) {return {P[2],-P[0],P[1]};}
inline Point NativeNormal(const std::array<Point,4>& Quad) {return Cross(Subtract(Quad[1],Quad[0]),Subtract(Quad[2],Quad[0]));}
inline bool CoincidentForwardQuads(const std::array<Point,4>& A,const std::array<Point,4>& B) {
    const Point NA=NativeNormal(A),NB=NativeNormal(B);
    double Dot=0,LA=0,LB=0;for(int Axis=0;Axis<3;++Axis) {Dot+=NA[Axis]*NB[Axis];LA+=NA[Axis]*NA[Axis];LB+=NB[Axis]*NB[Axis];}
    if(LA<1.e-16 || LB<1.e-16 || Dot<=0 || Dot*Dot<.9998*LA*LB) return false;
    for(const auto& P:A) {
        bool Found=false;
        for(const auto& Q:B) {const auto D=Subtract(P,Q);Found=Found || (D[0]*D[0]+D[1]*D[1]+D[2]*D[2]<1.e-12);}
        if(!Found) return false;
    }
    return true;
}
constexpr int CellSize=8;
struct Box {int x,y,z,sx,sy,sz;};
inline int Index(int x,int y,int z) {return x+(z<<3)+(y<<6);}
inline std::vector<Box> PackSolid(std::array<bool,512> occupied) {
    std::vector<Box> boxes;
    for(int y=0;y<8;++y) for(int z=0;z<8;++z) for(int x=0;x<8;++x) {
        if(!occupied[Index(x,y,z)]) continue;
        int sx=1,sz=1,sy=1;
        while(x+sx<8 && occupied[Index(x+sx,y,z)]) ++sx;
        while(z+sz<8) {bool filled=true;for(int a=0;a<sx;++a) filled=filled && occupied[Index(x+a,y,z+sz)];if(!filled) break;++sz;}
        while(y+sy<8) {bool filled=true;for(int b=0;b<sz;++b) for(int a=0;a<sx;++a) filled=filled && occupied[Index(x+a,y+sy,z+b)];if(!filled) break;++sy;}
        for(int c=0;c<sy;++c) for(int b=0;b<sz;++b) for(int a=0;a<sx;++a) occupied[Index(x+a,y+c,z+b)]=false;
        boxes.push_back({x,y,z,sx,sy,sz});
    }
    return boxes;
}
inline bool CullNativeFace(const std::array<int,3>& direction,bool neighborOpaque,bool modelOffsetIsZero) {
    const int axes=(direction[0]!=0)+(direction[1]!=0)+(direction[2]!=0);
    return neighborOpaque && modelOffsetIsZero && axes==1 && direction[0]>=-1 && direction[0]<=1
        && direction[1]>=-1 && direction[1]<=1 && direction[2]>=-1 && direction[2]<=1;
}
}
