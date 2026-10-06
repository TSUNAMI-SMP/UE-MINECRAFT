#pragma once

// Engine-independent presentation math. Values below are from Minecraft Java
// 1.21.11's HeldItemRenderer, PlayerEntityModel and default full-block display.
// Kept independent of UE so the production math can also be tested in the cloud.
#include <algorithm>
#include <cmath>

namespace BridgeCharacterMath {
constexpr double Pi = 3.14159265358979323846;
constexpr double StandingEyeCm = 162.0;
constexpr double CrouchedEyeCm = 127.0;
constexpr double FirstPersonBlockScale = 0.40;
constexpr double ThirdPersonBlockScale = 0.375;
constexpr double SwingSeconds = 0.30;
constexpr double FirstPersonVerticalFov = 70.0;

inline double Clamp(double Value,double Low,double High) { return std::max(Low,std::min(Value,High)); }
inline double WrapDegrees(double Value) {
    Value=std::fmod(Value+180.0,360.0);
    if(Value<0) Value+=360.0;
    return Value-180.0;
}
inline double Smooth(double Current,double Target,double Rate,double DeltaSeconds) {
    return Current+(Target-Current)*(1.0-std::exp(-Rate*std::max(0.0,DeltaSeconds)));
}
// MC's per-tick FOV smoothing approaches the target by half at 20 Hz.
inline double SprintFovMultiplier(double Current,bool Sprinting,double DeltaSeconds) {
    return Smooth(Current,Sprinting ? 1.15 : 1.0,20.0*std::log(2.0),DeltaSeconds);
}
inline double HorizontalFov(double VerticalFov,double Aspect=16.0/9.0) {
    return 2.0*std::atan(std::tan(VerticalFov*Pi/360.0)*Aspect)*180.0/Pi;
}
// Vanilla leaves the body in place at rest. Backwards walking keeps the torso
// facing the head, and reverses the gait instead of turning the body 180 degrees.
inline double BodyYaw(double Current,double View,double MovementYaw,double SpeedCm,
    bool Swinging,bool Blocking,double DeltaSeconds) {
    double Target=Current;
    if(SpeedCm>100.0) {
        Target=MovementYaw;
        if(std::abs(WrapDegrees(Target-View))>95.0) Target+=180.0;
    }
    if(Swinging) Target=View;
    const double Blended=Current+WrapDegrees(Target-Current)*(1.0-std::exp(20.0*std::log(0.7)*std::max(0.0,DeltaSeconds)));
    const double NeckLimit=Blocking ? 15.0 : 50.0;
    return WrapDegrees(View-Clamp(WrapDegrees(View-Blended),-NeckLimit,NeckLimit));
}
// Our cuboids extend down -Z. UE positive Pitch raises their hands towards +X;
// this is the opposite sign to vanilla's ModelPart.pitch (down is model +Y).
inline double AttackPitch(double Swing,double ViewPitchDegrees) {
    Swing=Clamp(Swing,0.0,1.0);
    const double Ease=1.0-std::pow(1.0-Swing,4.0);
    return (1.2*std::sin(Ease*Pi)+0.75*(0.7+ViewPitchDegrees*Pi/180.0)*std::sin(Swing*Pi))*180.0/Pi;
}

struct Vector {
    double X=0,Y=0,Z=0;
    Vector operator+(const Vector& Other) const {return {X+Other.X,Y+Other.Y,Z+Other.Z};}
    Vector operator*(double Scale) const {return {X*Scale,Y*Scale,Z*Scale};}
};
// Exact perspective equivalence in a single capture: x is camera depth. Scale
// transverse coordinates, after the complete model pose, to cancel world FOV.
// A nonuniform parent component scale cannot do this for rotated children
// because FTransform cannot retain the required shear.
inline double FirstPersonTransverseScale(double WorldVerticalFov) {
    const double Fov=std::isfinite(WorldVerticalFov) ? Clamp(WorldVerticalFov,30.0,160.0) : 80.0;
    return std::tan(Fov*Pi/360.0)/std::tan(FirstPersonVerticalFov*Pi/360.0);
}
inline Vector ProjectFirstPersonPoint(const Vector& CameraPoint,double WorldVerticalFov) {
    const double Scale=FirstPersonTransverseScale(WorldVerticalFov);
    return {CameraPoint.X,CameraPoint.Y*Scale,CameraPoint.Z*Scale};
}
inline Vector ProjectFirstPersonNormal(const Vector& CameraNormal,double WorldVerticalFov) {
    const double Scale=FirstPersonTransverseScale(WorldVerticalFov);
    const Vector N{CameraNormal.X,CameraNormal.Y/Scale,CameraNormal.Z/Scale};
    const double Length=std::sqrt(N.X*N.X+N.Y*N.Y+N.Z*N.Z);
    return Length>1e-12 ? N*(1.0/Length) : Vector{0,0,1};
}
struct Quaternion {
    double X=0,Y=0,Z=0,W=1;
    Quaternion operator*(const Quaternion& Other) const {
        return {W*Other.X+X*Other.W+Y*Other.Z-Z*Other.Y,
            W*Other.Y-X*Other.Z+Y*Other.W+Z*Other.X,
            W*Other.Z+X*Other.Y-Y*Other.X+Z*Other.W,
            W*Other.W-X*Other.X-Y*Other.Y-Z*Other.Z};
    }
    Vector Rotate(const Vector& V) const {
        const Quaternion Result=(*this)*Quaternion{V.X,V.Y,V.Z,0}*Quaternion{-X,-Y,-Z,W};
        return {Result.X,Result.Y,Result.Z};
    }
};
struct Pose {
    Vector Position;
    Quaternion Rotation;
    void Translate(double X,double Y,double Z) {Position=Position+Rotation.Rotate({X,Y,Z});}
    void Rotate(double X,double Y,double Z,double Degrees) {
        const double Half=Degrees*Pi/360.0,Sin=std::sin(Half);
        Rotation=Rotation*Quaternion{X*Sin,Y*Sin,Z*Sin,std::cos(Half)};
    }
};
inline Pose ToCamera(const Pose& Minecraft,const Quaternion& MeshBasis) {
    // MC renderer camera: right +X, up +Y, back +Z. UE: forward +X,
    // right +Y, up +Z. Reflection changes the signs of quaternion axes.
    return {{-Minecraft.Position.Z*100.0,Minecraft.Position.X*100.0,Minecraft.Position.Y*100.0},
        Quaternion{Minecraft.Rotation.Z,-Minecraft.Rotation.X,-Minecraft.Rotation.Y,Minecraft.Rotation.W}*MeshBasis};
}
inline Pose FirstPersonArm(double Swing,double Equipped,bool LeftHanded,bool Slim) {
    Swing=Clamp(Swing,0.0,1.0);Equipped=Clamp(Equipped,0.0,1.0);
    const double Side=LeftHanded ? -1.0 : 1.0,Root=std::sqrt(Swing);
    const double Lift=std::sin(Root*Pi),Across=std::sin(Swing*Swing*Pi);
    Pose P;
    P.Translate(Side*(0.64-0.3*Lift),-0.6-0.6*(1.0-Equipped)+0.4*std::sin(Root*2.0*Pi),-0.72-0.4*std::sin(Swing*Pi));
    P.Rotate(0,1,0,Side*45.0);P.Rotate(0,1,0,Side*70.0*Lift);P.Rotate(0,0,1,-Side*20.0*Across);
    P.Translate(-Side,3.6,3.5);P.Rotate(0,0,1,Side*120.0);P.Rotate(1,0,0,200.0);P.Rotate(0,1,0,-Side*135.0);
    P.Translate(Side*5.6,0,0);
    // ModelPart's shoulder origin and idle roll, then the centered cuboid's top.
    P.Translate(-Side*5.0/16.0,2.0/16.0,0);P.Rotate(0,0,1,Side*0.1*180.0/Pi);
    P.Translate(-Side*(Slim ? 0.5 : 1.0)/16.0,-2.0/16.0,0);
    return ToCamera(P,Quaternion{1,0,0,0}); // model front -Z/down +Y -> UE +X/-Z
}
inline Pose FirstPersonBlock(double Swing,double Equipped,bool LeftHanded) {
    Swing=Clamp(Swing,0.0,1.0);Equipped=Clamp(Equipped,0.0,1.0);
    const double Side=LeftHanded ? -1.0 : 1.0,Root=std::sqrt(Swing);
    const double Lift=std::sin(Root*Pi),Across=std::sin(Swing*Swing*Pi);
    Pose P;
    P.Translate(Side*0.56,-0.52-0.6*(1.0-Equipped),-0.72);
    P.Translate(-Side*0.4*Lift,0.2*std::sin(Root*2.0*Pi),-0.2*std::sin(Swing*Pi));
    P.Rotate(0,1,0,Side*(45.0-20.0*Across));P.Rotate(0,0,1,-Side*20.0*Lift);
    P.Rotate(1,0,0,-80.0*Lift);P.Rotate(0,1,0,-Side*45.0);
    // The left display is [0,225,0]; the item renderer mirrors its Y rotation.
    P.Rotate(0,1,0,LeftHanded ? -225.0 : 45.0);
    return ToCamera(P,Quaternion{0,0,1,0}); // imported cube has world-block axes
}
inline Pose ThirdPersonBlock(bool LeftHanded) {
    const double Side=LeftHanded ? -1.0 : 1.0;
    Pose P;
    // HeldItemFeatureRenderer grip followed by block.json's third-person display.
    P.Rotate(1,0,0,-90.0);P.Rotate(0,1,0,180.0);P.Translate(Side/16.0,0.125,-0.625);
    P.Translate(0,2.5/16.0,0);P.Rotate(1,0,0,75.0);P.Rotate(0,1,0,Side*45.0);
    // MC model arm: front -Z, right -X, up -Y; our arm: +X,+Y,+Z.
    return {{-P.Position.Z*100.0,-P.Position.X*100.0,-P.Position.Y*100.0},
        Quaternion{P.Rotation.Z,P.Rotation.X,P.Rotation.Y,P.Rotation.W}*Quaternion{0,1,0,0}};
}
}
