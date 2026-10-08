#pragma once
#include <algorithm>
#include <cmath>

// Unit-converted 1.21.11 LivingEntity.addDeathParticles, ExplosionSmokeParticle,
// Particle.tick/move and ParticleSpriteManager.SimpleSpriteProvider arithmetic.
// Coordinates here are UE centimetres; velocity is centimetres per MC tick.
namespace BridgeDeathPoofMath {
constexpr int Count=20;
inline double Unit(double Value) {return std::max(0.,std::min(Value,1.));}
inline double OffsetCm(double WidthCm,double Uniform,double Gaussian) {
    return (2*Unit(Uniform)-1)*WidthCm-Gaussian*20;
}
inline double BodyYcm(double HeightCm,double Uniform,double Gaussian) {
    return Unit(Uniform)*HeightCm-Gaussian*20;
}
inline double VelocityCmPerTick(double Gaussian,double Uniform) {
    return Gaussian*2+(2*Unit(Uniform)-1)*5;
}
inline double Gray(double Uniform) {return Unit(Uniform)*.3+.7;}
inline double QuadSizeCm(double First,double Second) {return 20*(Unit(First)*Unit(Second)*6+1);}
inline int LifetimeTicks(double Uniform) {return int(16/(Unit(Uniform)*.8+.2))+2;}
inline int Frame(int Age,int Lifetime,int FrameCount) {
    return FrameCount>0 && Lifetime>0 ? std::clamp(Age,0,Lifetime)*(FrameCount-1)/Lifetime : 0;
}
struct Vector {double x=0,y=0,z=0;};
struct Particle {
    Vector position,previous,velocity;
    int age=0,lifetime=18;
    bool stopped=false,onGround=false;
    // Collision resolves the requested per-tick displacement, with an AABB
    // 20cm wide/high extending upward from position, just as Particle.setPos.
    template<class Resolve> bool Tick(Resolve&& Collision) {
        previous=position;
        if(age++>=lifetime) return false;
        velocity.z+=.4; // -0.04 * -0.1 blocks/tick (buoyancy, not block dust).
        if(!stopped) {
            const Vector wanted=velocity,actual=Collision(position,wanted);
            position.x+=actual.x;position.y+=actual.y;position.z+=actual.z;
            if(std::abs(wanted.z)>=.001 && std::abs(actual.z)<.001) stopped=true;
            onGround=wanted.z!=actual.z && wanted.z<0;
            if(wanted.x!=actual.x) velocity.x=0;
            if(wanted.y!=actual.y) velocity.y=0;
        }
        velocity.x*=.9;velocity.y*=.9;velocity.z*=.9;
        if(onGround) {velocity.x*=.7;velocity.y*=.7;}
        return true;
    }
};
}
