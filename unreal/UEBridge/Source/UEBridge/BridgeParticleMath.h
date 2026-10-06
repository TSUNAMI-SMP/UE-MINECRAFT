#pragma once
#include <algorithm>
#include <cmath>

// Native 1.21.11: ClientWorld.addBlockBreakParticles subdivides each outline
// box into max(2,ceil(min(1,extent)/.25)) parts per axis. BillboardParticle's
// .1.. .2 half-size is halved by BlockDustParticle: full quad = 10..20 cm.
namespace BridgeParticleMath {
inline double Bounded(double Value,double Low,double High,double Fallback) {
    return std::isfinite(Value) ? std::max(Low,std::min(Value,High)) : Fallback;
}
inline double SizeMultiplier(double Value) {return Bounded(Value,.25,2,.75);}
inline double DensityMultiplier(double Value) {return Bounded(Value,0,1,1);}
inline double LifetimeMultiplier(double Value) {return Bounded(Value,.25,2,.9);}
inline int Subdivisions(double Extent) {
    return std::max(2,int(std::ceil(Bounded(Extent,0,1,1)*4)));
}
inline int BoxCount(double X,double Y,double Z) {return Subdivisions(X)*Subdivisions(Y)*Subdivisions(Z);}
inline int SelectedCount(int NativeCount,double Density) {
    return int(std::floor(std::max(0,NativeCount)*DensityMultiplier(Density)+.5));
}
inline bool SelectCell(int Index,int NativeCount,int Selected) {
    return NativeCount>0 && (Index+1)*Selected/NativeCount>Index*Selected/NativeCount;
}
inline double QuadSizeCm(double RandomUnit,double Multiplier) {
    return (10+10*Bounded(RandomUnit,0,1,.5))*SizeMultiplier(Multiplier);
}
inline int LifetimeTicks(double RandomUnit,double Multiplier) {
    const int Native=int(4/(Bounded(RandomUnit,0,1,.5)*.9+.1));
    return std::max(1,std::min(80,int(std::floor(Native*LifetimeMultiplier(Multiplier)+.5))));
}
}
