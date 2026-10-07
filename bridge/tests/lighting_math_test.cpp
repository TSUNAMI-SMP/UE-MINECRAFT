#include "BridgeLightingMath.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace BridgeLightingMath;
static void settle(Field& f) {for(int i=0;i<1000 && f.pending();++i) f.step(4096);assert(f.pending()==0);}
int main() {
    Field f;assert(!f.reset({0,0,0},{256,10,10}));assert(f.reset({-8,0,-8},{8,8,8}));
    f.initialize();settle(f);assert(f.sample({0,1,0}).sky==15);
    // Roof blocks direct sun but side light still reaches its edge.
    for(int x=-8;x<=8;++x) for(int z=-8;z<=8;++z) f.set({x,5,z},15,0);
    settle(f);assert(f.sample({0,4,0}).sky==0);assert(f.sample({0,6,0}).sky==15);
    f.set({0,5,0},0,0);settle(f);assert(f.sample({0,4,0}).sky==15);assert(f.sample({1,4,0}).sky==14);
    f.set({0,5,0},15,0);settle(f);assert(f.sample({0,4,0}).sky==0);
    // Emissive voxel attenuation and complete removal without persistent seeded ghost light.
    f.set({0,1,0},0,14);settle(f);assert(f.sample({0,1,0}).block==14);assert(f.sample({3,1,0}).block==11);
    f.set({1,1,0},15,0);settle(f);assert(f.sample({1,1,0}).block==0);assert(f.sample({2,1,0}).block==10);
    f.set({0,1,0},0,0);settle(f);assert(f.sample({3,1,0}).block==0);
    // Native solid faces of an opacity-zero slab still stop vertical skylight.
    for(int x=-8;x<=8;++x) for(int z=-8;z<=8;++z) f.set({x,5,z},0,0,1<<2);
    settle(f);assert(f.sample({0,4,0}).sky==0);assert(f.sample({0,5,0}).sky==15);
    // Transparent full-cube glass uses no shape mask; its opacity governs the light.
    for(int x=-8;x<=8;++x) for(int z=-8;z<=8;++z) f.set({x,5,z},0,0,0);
    settle(f);assert(f.sample({0,4,0}).sky==15);
    // MC boundary cap keeps a cave dark when its roof lies above the imported vertical slab.
    Field cave;assert(cave.reset({0,0,0},{4,4,4}));
    for(int x=0;x<5;++x) for(int z=0;z<5;++z) cave.setBoundary(x,z,0);
    cave.seed({1,1,1},0,10);cave.initialize();settle(cave);assert(cave.sample({2,2,2}).sky==0);assert(cave.sample({1,1,1}).block==0);
    assert(cave.reset({0,0,0},{4,4,4},false));cave.initialize();settle(cave);assert(cave.sample({2,2,2}).sky==0);
    // Streaming can scan sun columns over multiple frame budgets, keeping direct setup bounded.
    cave.reset({0,0,0},{15,15,15});cave.beginInitialize();assert(cave.pending()>0);
    assert(cave.step(32)<=48);assert(cave.pending()>0);settle(cave);assert(cave.sample({8,8,8}).sky==15);
    assert(FaceShade(0,1,0)==1);assert(FaceShade(0,-1,0)==.5f);assert(FaceShade(1,0,0)==.6f);assert(FaceShade(0,0,1)==.8f);
    assert(AO(false,false,false)==1);assert(AO(true,true,false)==.4f);assert(AO(true,false,true)<AO(true,false,false));
    // Opaque neighbors report packed zero light. Vanilla reuses the exposed face,
    // rather than reducing bright daylight/torch levels towards zero at corners.
    auto corner=CornerLight({15,0},{0,0},{0,0},{0,0});assert(corner[0]==1 && corner[1]==0);
    corner=CornerLight({0,12},{0,0},{0,8},{0,4});assert(corner[0]==0 && std::abs(corner[1]-.6f)<.00001f);
    assert(std::abs(AO(true,true,true)-(1+.2f+.2f+.2f)/4)<.00001f);
    Environment day,night;night.skyFactor=.05f;night.gamma=day.gamma=.5f;
    auto daylight=Lightmap(1,0,day),moonlight=Lightmap(1,0,night),lamp=Lightmap(0,10.f/15,night),black=Lightmap(0,0,night);
    assert(daylight[0]>moonlight[0]);assert(lamp[0]>black[0]);assert(lamp[0]>lamp[2]);
    night.gamma=1;auto bright=Lightmap(0,.5f,night);night.gamma=0;auto dim=Lightmap(0,.5f,night);assert(bright[0]>dim[0]);
    std::cout<<"Lighting assertions passed: sun, occlusion, edits, torch removal, native boundary, dimensions, AO and lightmap\n";
}
