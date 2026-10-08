#pragma once
#include <cmath>
namespace BridgeMovementMath {
struct Axis { double velocity, distance; };
// Minecraft moves with the accelerated velocity, then applies drag each 20 Hz
// tick. This closed form matches a tick exactly and composes across UE frames.
inline Axis flight(double velocity,double impulsePerTick,double retention,double seconds) {
    const double pre=impulsePerTick/(1-retention),post=pre*retention;
    const double decay=std::pow(retention,20*seconds);
    return {post+(velocity-post)*decay,pre*seconds+(velocity-post)*(1-decay)/(20*(1-retention))};
}
}
