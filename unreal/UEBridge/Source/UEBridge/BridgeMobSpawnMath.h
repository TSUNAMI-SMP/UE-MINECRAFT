#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

/** Bounded candidates around a requested ground location; collision/floor tests remain in UE. */
namespace BridgeMobSpawnMath {
inline std::vector<std::array<double,3>> Candidates(double Radius) {
    const double Step=std::max(75.0,Radius*2.0+12.0);
    std::vector<std::array<double,3>> Out{{0,0,0}};
    for(int Ring=1;Ring<=2;Ring++) for(int Direction=0;Direction<8;Direction++) {
        const double Angle=Direction*3.14159265358979323846/4.0;
        Out.push_back({std::cos(Angle)*Step*Ring,std::sin(Angle)*Step*Ring,0});
    }
    return Out;
}
inline bool CapsulesOverlap(double HorizontalDistanceSquared,double VerticalDistance,double RadiusA,double HalfA,double RadiusB,double HalfB) {
    const double Gap=std::max(0.0,std::abs(VerticalDistance)-std::max(0.0,HalfA-RadiusA)-std::max(0.0,HalfB-RadiusB));
    const double Sum=RadiusA+RadiusB;
    return HorizontalDistanceSquared+Gap*Gap<Sum*Sum;
}
}
