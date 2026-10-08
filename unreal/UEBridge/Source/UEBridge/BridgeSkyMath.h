#pragma once
#include <array>
#include <cmath>
#include <string_view>

namespace BridgeSkyMath {
constexpr double Pi=3.14159265358979323846;
constexpr double DayTicks=24000.;
constexpr double CelestialRadius=450000.;

inline double PositiveRemainder(double Value,double Period) {
    if(!std::isfinite(Value) || !std::isfinite(Period) || Period<=0) return 0;
    const double Result=std::fmod(Value,Period);
    return Result<0?Result+Period:Result;
}

// Minecraft 1.21.11's default day timeline uses this cubic Bezier curve.
// Four Newton steps match EasingType.CubicBezier; this is not the older
// cosine-based celestial-angle formula. Explicit exported angles take priority.
inline double DefaultDayEasing(double Progress) {
    if(!std::isfinite(Progress)) return 0;
    Progress=Progress<0?0:Progress>1?1:Progress;
    constexpr double XA=3*.362-3*.638+1,XB=-6*.362+3*.638,XC=3*.362;
    constexpr double YA=3*.241-3*.759+1,YB=-6*.241+3*.759,YC=3*.241;
    double T=Progress;
    for(int I=0;I<4;++I) {
        const double Derivative=(3*XA*T+2*XB)*T+XC;
        if(Derivative<1e-5) break;
        T-=(((XA*T+XB)*T+XC)*T-Progress)/Derivative;
    }
    return ((YA*T+YB)*T+YC)*T;
}

inline double DefaultSunDegrees(double TimeOfDay) {
    if(!std::isfinite(TimeOfDay)) TimeOfDay=6000.;
    const double Wrapped=PositiveRemainder(TimeOfDay,DayTicks);
    return DefaultDayEasing(PositiveRemainder(Wrapped-6000.,DayTicks)/DayTicks)*360.;
}

inline int DefaultMoonPhase(double TimeOfDay) {
    // Reduce first so long-lived or negative worlds cannot overflow a 32-bit day.
    return int(std::floor(PositiveRemainder(TimeOfDay,8*DayTicks)/DayTicks));
}

inline const char* MoonPhaseTextureName(int Phase) {
    static constexpr std::array<const char*,8> Names={"full_moon","waning_gibbous","third_quarter","waning_crescent","new_moon","waxing_crescent","first_quarter","waxing_gibbous"};
    return Names[Phase>=0 && Phase<8?Phase:0];
}

inline bool HasCelestialBodies(std::string_view Dimension,bool HasSkyLight,std::string_view Skybox={}) {
    // End has skylight in 1.21.11 but its own skybox, without a sun or moon.
    // The optional exported skybox also handles custom dimension definitions.
    if(!Skybox.empty()) return Skybox=="overworld";
    return HasSkyLight && Dimension!="minecraft:the_nether" && Dimension!="minecraft:the_end";
}

inline std::array<double,3> Direction(double AngleDegrees) {
    const double Angle=PositiveRemainder(AngleDegrees,360.)*Pi/180.;
    // Minecraft rotates the vertical quad toward west; MC x/y/z maps to UE x/z/y.
    return {-std::sin(Angle),0.,std::cos(Angle)};
}

inline double PlaneScale(bool Moon) {
    // Vanilla's quad widths are 60 (sun) and 40 (moon) at radius 100.
    // UE's basic plane is 100 units wide; preserve their apparent angular sizes.
    return CelestialRadius*(Moon?.4:.6)/100.;
}
}
