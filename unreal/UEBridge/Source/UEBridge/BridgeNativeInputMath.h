#pragma once
#include <algorithm>
#include <cmath>
#include <string_view>

/** Minecraft's mouse sensitivity curve and small input-state rules, independent of UE. */
namespace BridgeNativeInputMath {
enum class MouseButton {Unbound,Left,Right,Middle,Side4,Side5};
inline MouseButton MouseTranslation(std::string_view Key) {
    if(Key=="key.mouse.left" || Key=="key.mouse.0") return MouseButton::Left;
    if(Key=="key.mouse.right" || Key=="key.mouse.1") return MouseButton::Right;
    if(Key=="key.mouse.middle" || Key=="key.mouse.2") return MouseButton::Middle;
    if(Key=="key.mouse.3") return MouseButton::Side4;
    if(Key=="key.mouse.4") return MouseButton::Side5;
    return MouseButton::Unbound;
}
inline double MouseDegreesPerCount(double Sensitivity) {
    const double S=std::isfinite(Sensitivity) ? std::max(0.0,std::min(1.0,Sensitivity)) : .5;
    const double Curve=S*.6+.2;
    return Curve*Curve*Curve*8.0*.15;
}
inline double ClampPitch(double Pitch) {return std::max(-90.0,std::min(90.0,Pitch));}
inline int WrapSlot(int Slot) {const int Result=Slot%9;return Result<0 ? Result+9 : Result;}
/** Preserve fractional wheel steps, as Minecraft's scroll accumulator does. */
inline int WheelSteps(double Delta,double Sensitivity,double& Remainder) {
    if(!std::isfinite(Delta) || !std::isfinite(Sensitivity) || Sensitivity<0) return 0;
    if(!std::isfinite(Remainder) || Remainder*Delta<0) Remainder=0;
    Remainder+=Delta*std::min(100.0,Sensitivity);
    const double Whole=Remainder>=0 ? std::floor(Remainder) : std::ceil(Remainder);
    const int Steps=int(std::max(-10000.0,std::min(10000.0,Whole)));
    Remainder-=Steps;
    return Steps;
}
inline bool IsDoubleTap(double Previous,double Now) {return Previous>=0 && Now>=Previous && Now-Previous<=.35;}
}
