"""Compile source-derived mob control decisions without requiring Unreal."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "BridgeMobAIMath.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using namespace BridgeMobAIMath;
    // GoalSelector starts ordinary goals every other tick; WanderAroundGoal
    // uses ceil(120/2). The mean start interval stays 120 entity ticks.
    assert(goalChance(120)==60 && goalChance(41)==21);
    assert(profile("minecraft:cow").wanderSpeed==1);
    assert(profile("minecraft:creeper").wanderSpeed==.8);
    assert(profile("minecraft:zombie").followRange==35);
    assert(profile("minecraft:bat").locomotion==Locomotion::Bat);
    assert(profile("minecraft:salmon").wanderChance==40);
    // A supported one-block step requests a jump. Walls, lost support,
    // blocked heads, cooldown and distant waypoints must never loop jumps.
    assert(canJump(1,.25,.6,true,true,false,0));
    assert(!canJump(2,.25,.6,true,true,true,0));
    assert(!canJump(1,.25,.6,true,false,false,0));
    assert(!canJump(1,.25,.6,false,true,false,0));
    assert(!canJump(1,.25,.6,true,true,false,1));
    assert(!canJump(1,4,.6,true,true,false,0));
    assert(!canJump(.5,.25,.6,true,true,false,0));
    assert(std::abs(attackRangeBlocks()-(std::sqrt(2.04)-.6))<1e-12);
    // Independently evaluate the source BatEntity exponential controller
    // after 20 mob ticks, and FishMoveControl lerp .125 after 8 ticks.
    double bat=0;
    for(int i=0;i<20;i++) bat=batComponent(bat,1,false);
    assert(std::abs(bat-(.5*(1-std::pow(.9,20))))<1e-12);
    assert(std::abs(batComponent(0,-1,true)+.07)<1e-12);
    double fish=0;
    for(int i=0;i<8;i++) fish=fishSpeed(fish,.25,1);
    assert(std::abs(fish-(.25*(1-std::pow(.875,8))))<1e-12);
    std::cout << "Mob AI math: goal scheduling, obstacle jump gating, bat and fish controls passed\n";
}
'''

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="bridge-mob-ai-") as temporary:
        source = pathlib.Path(temporary) / "check.cpp"
        binary = pathlib.Path(temporary) / "check"
        source.write_text(SOURCE)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(ROOT / "unreal/UEBridge/Source/UEBridge"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
