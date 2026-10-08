#pragma once
#include <array>
#include <cmath>
#include <string_view>
#include <cstdint>
#include <vector>

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
    // SkyRendering rotates Y(-90) then X(angle). BridgeProtocol maps
    // Minecraft x/y/z to Unreal z/-x/y: west (-MC x) is therefore +UE y.
    return {0.,std::sin(Angle),std::cos(Angle)};
}

inline double PlaneScale(bool Moon) {
    // Vanilla's quad widths are 60 (sun) and 40 (moon) at radius 100.
    // UE's basic plane is 100 units wide; preserve their apparent angular sizes.
    return CelestialRadius*(Moon?.4:.6)/100.;
}

struct StarQuad { std::array<std::array<double,3>,4> vertices; };
/** SkyRendering.createStars: same CheckedRandom seed, draws and rejected points.
 * Coordinates remain Minecraft x/y/z until the rendering adapter maps axes.
 */
inline std::vector<StarQuad> Stars() {
    uint64_t seed=(10842ULL^0x5deece66dULL)&((1ULL<<48)-1);
    auto bits=[&](int count) {seed=(seed*0x5deece66dULL+0xbULL)&((1ULL<<48)-1);return uint32_t(seed>>(48-count));};
    auto nextFloat=[&]() {return float(bits(24))/float(1<<24);};
    auto nextDouble=[&]() {const uint64_t high=bits(26),low=bits(27);return double((high<<27)+low)/double(1ULL<<53);};
    std::vector<StarQuad> result;
    for(int index=0;index<1500;++index) {
        const float x=nextFloat()*2-1,y=nextFloat()*2-1,z=nextFloat()*2-1;
        const float halfWidth=.15f+nextFloat()*.1f;
        const float length=std::sqrt(x*x+y*y+z*z);
        if(length<=.010000001f || length>=1.f) continue;
        const std::array<double,3> center{x/length*100.,y/length*100.,z/length*100.};
        const double angle=float(nextDouble()*float(Pi)*2.);
        const double tangentLength=std::sqrt(double(x)*x+double(z)*z);
        const std::array<double,3> right{-z/tangentLength,0,x/tangentLength};
        const std::array<double,3> towards{-x/length,-y/length,-z/length};
        const std::array<double,3> up{towards[1]*right[2],towards[2]*right[0]-towards[0]*right[2],-towards[1]*right[0]};
        StarQuad quad;
        constexpr int signs[4][2]={{1,-1},{1,1},{-1,1},{-1,-1}};
        for(int vertex=0;vertex<4;++vertex) {
            const double a=halfWidth*signs[vertex][0],b=halfWidth*signs[vertex][1];
            const double rotatedX=std::cos(angle)*a+std::sin(angle)*b,rotatedY=-std::sin(angle)*a+std::cos(angle)*b;
            for(int axis=0;axis<3;++axis) quad.vertices[vertex][axis]=center[axis]+right[axis]*rotatedX+up[axis]*rotatedY;
        }
        result.push_back(quad);
    }
    return result;
}

inline std::array<double,3> RotateCelestial(const std::array<double,3>& Point,double AngleDegrees) {
    // SkyRendering first applies Y(-90deg), then X(angle) to MC coordinates.
    const double angle=AngleDegrees*Pi/180.,y=Point[1]*std::cos(angle)-Point[2]*std::sin(angle),z=Point[1]*std::sin(angle)+Point[2]*std::cos(angle);
    return {Point[0],z,y}; // After Y(-90), then bridge z/-x/y mapping.
}
}
