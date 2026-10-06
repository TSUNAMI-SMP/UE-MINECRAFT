#include "BridgeCharacterMath.h"
#include <iostream>
#include <string>

namespace {
int Passed=0,Failed=0;
void Check(bool Result,const char* Name) {
    if(Result) ++Passed;
    else {++Failed;std::cerr<<"FAIL: "<<Name<<'\n';}
}
bool Near(double A,double B,double Tolerance=1e-8) {return std::abs(A-B)<Tolerance;}
bool Same(const BridgeCharacterMath::Vector& A,const BridgeCharacterMath::Vector& B) {
    return Near(A.X,B.X)&&Near(A.Y,B.Y)&&Near(A.Z,B.Z);
}
}
int main() {
    using namespace BridgeCharacterMath;
    Check(Near(SprintFovMultiplier(1,true,.05),1.075),"sprint FOV approaches half target in one MC tick");
    double LowRate=1,HighRate=1;
    for(int I=0;I<30;++I) LowRate=SprintFovMultiplier(LowRate,true,1.0/30.0);
    for(int I=0;I<144;++I) HighRate=SprintFovMultiplier(HighRate,true,1.0/144.0);
    Check(Near(LowRate,HighRate),"FOV smoothing does not depend on render FPS");
    Check(Near(LowRate,1.15,1e-6),"80 degree sprint settles at 92 degrees");
    Check(Near(SprintFovMultiplier(LowRate,false,1),1,1e-6),"FOV recovers after neutral or stale authority");
    Check(Near(SprintFovMultiplier(1,true,-.1),1),"negative frame delta does not advance smoothing");
    Check(Near(HorizontalFov(80,1),80),"square capture keeps vertical and horizontal FOV equal");
    Check(HorizontalFov(80)>80&&HorizontalFov(92)>HorizontalFov(80),"16:9 preserves increasing vertical FOV");

    Check(Near(BodyYaw(0,30,0,0,false,false,.05),0),"head can turn at rest without turning torso");
    Check(Near(BodyYaw(0,80,0,0,false,false,.05),30),"torso follows only after 50 degree neck limit");
    Check(Near(BodyYaw(0,30,0,0,false,true,.05),15),"blocking reduces neck range to 15 degrees");
    Check(BodyYaw(0,0,90,432,false,false,.05)>0,"right strafe turns torso towards motion");
    Check(BodyYaw(0,0,-90,432,false,false,.05)<0,"left strafe turns torso towards motion");
    Check(Near(BodyYaw(0,0,180,432,false,false,.05),0),"backwards motion does not turn torso away from aim");
    Check(BodyYaw(40,0,90,432,true,false,.05)<40,"attack turns torso towards aim instead of sideways motion");
    Check(std::abs(WrapDegrees(BodyYaw(179,-179,-179,432,false,false,.05)-179))<3,"torso crosses yaw seam by short route");
    Check(Near(AttackPitch(0,0),0)&&Near(AttackPitch(1,0),0),"attack begins and ends in neutral pose");
    Check(AttackPitch(.35,0)>0,"downward cuboid swings forwards with positive UE pitch");
    Check(AttackPitch(.35,45)>AttackPitch(.35,-45),"attack lift responds to head pitch");

    const auto Right=FirstPersonArm(0,1,false,false),Left=FirstPersonArm(0,1,true,false);
    // Golden shoulder position computed from the vanilla matrix stack, not from
    // the former bridge's manually tuned coordinates.
    Check(Near(Right.Position.X,52.81231384,1e-6)&&Near(Right.Position.Y,47.86726326,1e-6)&&Near(Right.Position.Z,-84.34835369,1e-6),"vanilla empty-arm matrix chain golden pose");
    Check(Near(Right.Position.X,Left.Position.X)&&Near(Right.Position.Y,-Left.Position.Y)&&Near(Right.Position.Z,Left.Position.Z),"empty-arm shoulders mirror across camera right axis");
    const auto Hand=Right.Position+Right.Rotation.Rotate({0,0,-75});
    Check(Hand.X>0&&Hand.Y>0&&Hand.Z<0,"empty right hand extends forward into lower right view");
    Check(Same(FirstPersonArm(1,1,false,false).Position,Right.Position),"arm returns after complete swing");
    const auto Unequipped=FirstPersonArm(0,0,false,false);
    Check(Near(Unequipped.Position.X,Right.Position.X)&&Near(Unequipped.Position.Y,Right.Position.Y)&&Near(Unequipped.Position.Z,Right.Position.Z-60),"unequipped arm drops 60 cm");
    const auto Slim=FirstPersonArm(0,1,false,true);
    const auto SlimOffset=Vector{Slim.Position.X-Right.Position.X,Slim.Position.Y-Right.Position.Y,Slim.Position.Z-Right.Position.Z};
    Check(Near(std::sqrt(SlimOffset.X*SlimOffset.X+SlimOffset.Y*SlimOffset.Y+SlimOffset.Z*SlimOffset.Z),3.125),"slim arm centers its narrower cuboid at same shoulder");

    const auto Block=FirstPersonBlock(0,1,false),LeftBlock=FirstPersonBlock(0,1,true);
    Check(Same(Block.Position,{72,56,-52}),"equipped block uses vanilla .56/-.52/-.72 translation");
    Check(Same(LeftBlock.Position,{72,-56,-52}),"equipped left block mirrors position");
    Check(Same(FirstPersonBlock(0,0,false).Position,{72,56,-112}),"unequipped block drops by .6 blocks");
    Check(Same(FirstPersonBlock(1,1,false).Position,Block.Position),"block returns after complete swing");
    Check(FirstPersonBlock(.4,1,false).Position.Y<Block.Position.Y,"block swing moves across view");
    bool Normalized=true,Finite=true;
    for(int I=0;I<=100;++I) for(bool L:{false,true}) for(bool S:{false,true}) {
        const auto Pose=FirstPersonArm(I/100.0,1,L,S);const auto& Q=Pose.Rotation;
        Normalized=Normalized&&Near(Q.X*Q.X+Q.Y*Q.Y+Q.Z*Q.Z+Q.W*Q.W,1);
        Finite=Finite&&std::isfinite(Pose.Position.X)&&std::isfinite(Pose.Position.Y)&&std::isfinite(Pose.Position.Z);
    }
    Check(Normalized,"all handed/slim arm poses preserve unit rotations");
    Check(Finite,"all arm swing samples remain finite");
    Check(Near(FirstPersonBlockScale*100,40)&&Near(ThirdPersonBlockScale*100,37.5),"held full block has vanilla display scales");
    const auto ThirdBlock=ThirdPersonBlock(false),ThirdLeft=ThirdPersonBlock(true);
    Check(Same(ThirdBlock.Position,{28.125,6.25,-62.5}),"third block grip and model translation are composed at shoulder joint");
    Check(Same(ThirdLeft.Position,{28.125,-6.25,-62.5}),"third left grip mirrors across arm lateral axis");
    const auto& ThirdQ=ThirdBlock.Rotation;
    Check(Near(ThirdQ.X*ThirdQ.X+ThirdQ.Y*ThirdQ.Y+ThirdQ.Z*ThirdQ.Z+ThirdQ.W*ThirdQ.W,1),"third grip keeps a unit rotation after basis conversion");
    std::cout<<"Character production math: "<<Passed<<" passed, "<<Failed<<" failed\n";
    return Failed ? 1 : 0;
}
