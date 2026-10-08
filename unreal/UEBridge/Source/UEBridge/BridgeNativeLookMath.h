#pragma once
#include "BridgeNativeInputMath.h"
#include <algorithm>
#include <cmath>

// Minecraft 1.21.11 Mouse.updateMouse / Smoother, translated to C++.
// The calibration is applied after the vanilla curve: the user's requested
// former 250% response is the new 100% response without flattening the slider.
namespace BridgeNativeLookMath {
constexpr double Calibration = 2.5;
struct Smoother {
    double ActualSum=0,SmoothedSum=0,MovementLatency=0;
    void Clear() {ActualSum=SmoothedSum=MovementLatency=0;}
    double Smooth(double Original,double Factor) {
        ActualSum+=Original;
        double Difference=ActualSum-SmoothedSum;
        const double Average=(MovementLatency+Difference)*.5;
        const double Sign=Difference>0 ? 1 : Difference<0 ? -1 : 0;
        if(Sign*Difference>Sign*MovementLatency) Difference=Average;
        MovementLatency=Average;
        const double Result=Difference*Factor;
        SmoothedSum+=Result;
        return Result;
    }
};
inline double Degrees(double Counts,double Sensitivity,double DeltaSeconds,bool SmoothCamera,
    bool Spyglass,Smoother& State) {
    if(!std::isfinite(Counts) || !std::isfinite(DeltaSeconds)) {State.Clear();return 0;}
    const double Vanilla=BridgeNativeInputMath::MouseDegreesPerCount(Sensitivity);
    if(SmoothCamera) {
        const double Gain=Vanilla/.15;
        return State.Smooth(Counts*Gain,std::max(0.,DeltaSeconds)*Gain)*.15*Calibration;
    }
    State.Clear();
    return Counts*Vanilla*(Spyglass ? .125 : 1.)*Calibration;
}
}
