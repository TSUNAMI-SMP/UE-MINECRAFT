#pragma once
#include <cmath>
#include <algorithm>
namespace BridgeMovementMath {
struct Axis { double velocity, distance; };
struct Horizontal { double x, z; };
// ClientPlayerEntity applies .98 input friction and the sneaking attribute
// before direction-dependent normalization; diagonal sneak is not simply a
// capped 30 percent walking speed.
inline Horizontal movementInput(double x,double z,bool crouching) {
    const double keys=std::hypot(x,z);
    if(keys>1) {x/=keys;z/=keys;}
    const double scale=.98*(crouching ? .3 : 1.0);
    x*=scale;z*=scale;
    const double length=std::hypot(x,z);
    if(length<=0) return {0,0};
    const double a=std::abs(x)/length,b=std::abs(z)/length;
    const double ratio=std::min(a,b)/std::max(a,b);
    const double magnitude=std::min(1.0,length*std::sqrt(1+ratio*ratio));
    return {x*magnitude/length,z*magnitude/length};
}
// PlayerEntity checks X, Z, then both axes using the same 0.05-block
// decrement. The caller supplies collision checks for the box below the feet.
template<class Empty> inline Horizontal sneakMove(double x,double z,Empty empty) {
    const auto reduce=[](double value) {
        return std::abs(value)<=5.0 ? 0.0 : value-std::copysign(5.0,value);
    };
    while(x!=0 && empty(x,0)) x=reduce(x);
    while(z!=0 && empty(0,z)) z=reduce(z);
    while(x!=0 && z!=0 && empty(x,z)) {x=reduce(x);z=reduce(z);}
    return {x,z};
}
// Camera.updateEyeHeight runs at 20 Hz; Camera.update linearly interpolates
// the two tick states. Keep presentation separate from the gameplay eye ray.
struct Eye {
    double previous=162,current=162,remainder=0;
    void reset(double height) {previous=current=height;remainder=0;}
    double update(double target,double seconds) {
        remainder+=std::max(0.0,seconds);
        while(remainder+1e-12>=.05) {
            previous=current;current+=(target-current)*.5;remainder-=.05;
        }
        return previous+(current-previous)*std::clamp(remainder/.05,0.0,1.0);
    }
};
// Minecraft moves with the accelerated velocity, then applies drag each 20 Hz
// tick. This closed form matches a tick exactly and composes across UE frames.
inline Axis flight(double velocity,double impulsePerTick,double retention,double seconds) {
    const double pre=impulsePerTick/(1-retention),post=pre*retention;
    const double decay=std::pow(retention,20*seconds);
    return {post+(velocity-post)*decay,pre*seconds+(velocity-post)*(1-decay)/(20*(1-retention))};
}
}
