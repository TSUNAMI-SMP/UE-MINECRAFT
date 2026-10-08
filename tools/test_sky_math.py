"""Compile independent sky checks against the Minecraft 1.21.11 default data.

The easing reference numbers were evaluated with Mojang's EasingType.CubicBezier
using the day timeline's control points. This does not build Unreal or render.
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "BridgeSkyMath.h"
#include <cassert>
#include <iostream>
#include <limits>

bool Near(double A,double B,double Epsilon=1e-6) {return std::abs(A-B)<=Epsilon;}
int main() {
    using namespace BridgeSkyMath;
    int Checks=0;
    auto Check=[&](bool Passed) {assert(Passed);++Checks;};
    // Numeric fixtures from the versioned Mojang easing implementation, rather
    // than duplicating the production polynomial in the test.
    Check(Near(DefaultDayEasing(0),0));
    Check(Near(DefaultDayEasing(.25),.215635806,3e-7));
    Check(Near(DefaultDayEasing(.5),.5));
    Check(Near(DefaultDayEasing(.75),.784364223,3e-7));
    Check(Near(DefaultDayEasing(1),1));
    Check(Near(DefaultDayEasing(1./24),.029203538,3e-7));
    Check(Near(DefaultDayEasing(23./24),.970796525,3e-7));
    Check(Near(DefaultSunDegrees(6000),0));
    Check(Near(DefaultSunDegrees(18000),180));
    Check(Near(DefaultSunDegrees(0),282.37112028,.0002));
    Check(Near(DefaultSunDegrees(12000),77.62889016,.0002));
    Check(Near(DefaultSunDegrees(24000),DefaultSunDegrees(0)));
    Check(Near(DefaultSunDegrees(-24000),DefaultSunDegrees(0)));
    Check(Near(DefaultSunDegrees(6000.+24000.*100000000000.),0));
    Check(Near(DefaultSunDegrees(std::numeric_limits<double>::infinity()),0));
    Check(Near(DefaultSunDegrees(std::numeric_limits<double>::quiet_NaN()),0));
    // MC noon is up; 90 degrees points west and 270 degrees points east.
    auto Up=Direction(0),West=Direction(90),Down=Direction(180),East=Direction(270);
    Check(Near(Up[0],0) && Near(Up[2],1));
    Check(Near(West[0],0) && Near(West[1],1) && Near(West[2],0));
    Check(Near(Down[0],0) && Near(Down[2],-1));
    Check(Near(East[0],0) && Near(East[1],-1) && Near(East[2],0));
    Check(Near(Direction(-90)[1],East[1]));
    for(int Tick=0;Tick<24000;Tick+=113) {
        const auto Sun=Direction(DefaultSunDegrees(Tick)),Moon=Direction(DefaultSunDegrees(Tick)+180);
        Check(Near(Sun[0]*Sun[0]+Sun[1]*Sun[1]+Sun[2]*Sun[2],1));
        Check(Near(Sun[0]+Moon[0],0) && Near(Sun[1]+Moon[1],0) && Near(Sun[2]+Moon[2],0));
        Check(Near(DefaultSunDegrees(Tick),DefaultSunDegrees(Tick+24000)));
    }
    for(int Phase=0;Phase<8;++Phase) {
        Check(DefaultMoonPhase(Phase*24000.)==Phase);
        Check(DefaultMoonPhase(Phase*24000.+23999.)==Phase);
    }
    Check(DefaultMoonPhase(192000)==0);
    Check(DefaultMoonPhase(-1)==7);
    Check(DefaultMoonPhase(-24000)==7);
    Check(DefaultMoonPhase(-192000)==0);
    Check(DefaultMoonPhase(192000.*100000000000.+72000.)==3);
    Check(DefaultMoonPhase(std::numeric_limits<double>::infinity())==0);
    Check(std::string_view(MoonPhaseTextureName(0))=="full_moon");
    Check(std::string_view(MoonPhaseTextureName(1))=="waning_gibbous");
    Check(std::string_view(MoonPhaseTextureName(2))=="third_quarter");
    Check(std::string_view(MoonPhaseTextureName(3))=="waning_crescent");
    Check(std::string_view(MoonPhaseTextureName(4))=="new_moon");
    Check(std::string_view(MoonPhaseTextureName(5))=="waxing_crescent");
    Check(std::string_view(MoonPhaseTextureName(6))=="first_quarter");
    Check(std::string_view(MoonPhaseTextureName(7))=="waxing_gibbous");
    Check(std::string_view(MoonPhaseTextureName(-1))=="full_moon");
    // The End has skylight but no overworld celestial geometry in this version.
    Check(HasCelestialBodies("minecraft:overworld",true));
    Check(!HasCelestialBodies("minecraft:overworld",false));
    Check(!HasCelestialBodies("minecraft:the_nether",false));
    Check(!HasCelestialBodies("minecraft:the_end",true));
    Check(!HasCelestialBodies("custom:realm",true,"end"));
    Check(!HasCelestialBodies("custom:realm",true,"none"));
    Check(HasCelestialBodies("custom:realm",false,"overworld"));
    Check(Near(PlaneScale(false)*100/CelestialRadius,.6));
    Check(Near(PlaneScale(true)*100/CelestialRadius,.4));
    // Independent java.util.Random/CheckedRandom fixtures for createStars.
    const auto StarsGeometry=Stars();Check(StarsGeometry.size()==780);
    const double Centers[3][3]={{-53.246868134,69.925743103,47.698661804},{-96.890884399,-18.466674805,16.466272354},{-98.799293518,9.651536942,12.064331055}};
    const double Widths[3]={.150184259,.248971671,.197489306};
    for(int index=0;index<3;++index) {
        for(int axis=0;axis<3;++axis) {double center=0;for(const auto& vertex:StarsGeometry[index].vertices) center+=vertex[axis]/4.;Check(Near(center,Centers[index][axis],.00002));}
        double length=0;for(int axis=0;axis<3;++axis) length+=std::pow(StarsGeometry[index].vertices[0][axis]-StarsGeometry[index].vertices[1][axis],2);
        Check(Near(std::sqrt(length),2*Widths[index],.000002));
    }
    const auto CelestialWest=RotateCelestial({0,100,0},90);Check(Near(CelestialWest[0],0)&&Near(CelestialWest[1],100)&&Near(CelestialWest[2],0));
    std::cout << "Sky snapshot math: " << Checks << " checks passed\n";
}
'''

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="bridge-sky-math-") as temporary:
        source = pathlib.Path(temporary) / "check.cpp"
        binary = pathlib.Path(temporary) / "check"
        source.write_text(SOURCE)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "unreal/UEBridge/Source/UEBridge"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
