#pragma once
#include <array>
#include <algorithm>
#include "BridgeMovementMath.h"
namespace BridgeFluidMath {
inline BridgeMovementMath::Axis travel(double velocity,double input,double drag,double gravity,double seconds) {
    auto result=BridgeMovementMath::flight(velocity,input-gravity/drag,drag,seconds);
    result.distance+=gravity/drag*std::max(0.,seconds);return result;
}
// FluidRenderer weights nearly-full fluid columns tenfold. Solid samples (-1)
// do not pull a corner down; an empty column (0) does.
inline double corner(const std::array<double,4>& samples,bool above) {
    if(above) return 1;
    double sum=0,weight=0;
    for(double h:samples) {if(h<0) continue;if(h>=1) return 1;const double w=h>=.8 ? 10 : 1;sum+=h*w;weight+=w;}
    return weight>0 ? std::clamp(sum/weight,0.,1.) : 0;
}
}
