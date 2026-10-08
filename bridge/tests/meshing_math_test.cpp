#include "BridgeMeshingMath.h"
#include <cassert>
#include <iostream>
#include <random>
using namespace BridgeMeshingMath;
void exact(const std::array<bool,512>& source) {
    std::array<int,512> counts{};
    for(const auto& box:PackSolid(source)) {
        assert(box.x>=0 && box.y>=0 && box.z>=0 && box.sx>0 && box.sy>0 && box.sz>0);
        assert(box.x+box.sx<=8 && box.y+box.sy<=8 && box.z+box.sz<=8);
        for(int y=box.y;y<box.y+box.sy;++y) for(int z=box.z;z<box.z+box.sz;++z) for(int x=box.x;x<box.x+box.sx;++x) ++counts[Index(x,y,z)];
    }
    for(int i=0;i<512;++i) assert(counts[i]==int(source[i]));
}
int main() {
    std::array<bool,512> cube{};cube.fill(true);const auto full=PackSolid(cube);
    assert(full.size()==1 && full[0].sx==8 && full[0].sy==8 && full[0].sz==8);exact(cube);
    cube[Index(4,4,4)]=false;exact(cube);assert(PackSolid(cube).size()>1);
    std::array<bool,512> floor{};for(int x=0;x<8;++x) for(int z=0;z<8;++z) floor[Index(x,0,z)]=true;
    assert(PackSolid(floor).size()==1);exact(floor);std::array<bool,512> empty{};assert(PackSolid(empty).empty());
    std::mt19937 random(7779);for(unsigned n=0;n<128;++n) {std::array<bool,512> mask{};for(auto& value:mask) value=(random()%5)<n%5;exact(mask);}
    assert(CullNativeFace({1,0,0},true,true));assert(!CullNativeFace({1,0,0},false,true));
    assert(!CullNativeFace({0,0,0},true,true));assert(!CullNativeFace({1,0,1},true,true));assert(!CullNativeFace({1,0,0},true,false));
    // Native cube quads use the same outward vertex order as the model baker. The
    // reflected UE transform needs reverse triangles, with unchanged outward normal.
    const std::array<std::array<Point,4>,6> quads={{{{{0,0,1},{0,0,0},{1,0,0},{1,0,1}}},{{{0,1,0},{0,1,1},{1,1,1},{1,1,0}}},
        {{{1,1,0},{1,0,0},{0,0,0},{0,1,0}}},{{{0,1,1},{0,0,1},{1,0,1},{1,1,1}}},
        {{{0,1,0},{0,0,0},{0,0,1},{0,1,1}}},{{{1,1,1},{1,0,1},{1,0,0},{1,1,0}}}}};
    const std::array<Point,6> normals={{{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}}};
    for(int face=0;face<6;++face) {const auto& q=quads[face];assert(NativeNormal(q)==normals[face]);
        const Point ueNormal=MCToUE(NativeNormal(q));const Point a=MCToUE(q[0]),b=MCToUE(q[2]),c=MCToUE(q[1]);
        assert(Cross(Subtract(b,a),Subtract(c,a))==ueNormal);
        // Grass overlay has identical corners and orientation, even after a
        // cyclic reordering. Crossed foliage's reverse face must remain unbiased.
        auto cyclic=q;for(int i=0;i<4;++i) cyclic[i]=q[(i+1)%4];assert(CoincidentForwardQuads(q,cyclic));
        auto reverse=q;reverse[1]=q[3];reverse[3]=q[1];assert(!CoincidentForwardQuads(q,reverse));
        auto shifted=q;for(auto& point:shifted) point[0]+=.001;assert(!CoincidentForwardQuads(q,shifted));
        auto partial=q;partial[0]=partial[1];assert(!CoincidentForwardQuads(q,partial));}
    std::cout << "Meshing: 128 randomized collider round trips, native cull guards and six layered-quad orientation checks passed\n";
}
