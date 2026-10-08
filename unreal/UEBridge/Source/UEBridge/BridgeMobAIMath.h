#pragma once
#include <algorithm>
#include <cmath>
#include <string>

// MC 1.21.11: WanderAroundGoal/Goal, MoveControl, JumpControl,
// MeleeAttackGoal, BatEntity and FishEntity. UE-independent decisions only.
namespace BridgeMobAIMath {
enum class Locomotion { Ground, Bat, Fish, Squid, Flying };
struct Profile {
    Locomotion locomotion=Locomotion::Ground;
    double wanderSpeed=1, chaseSpeed=1, panicSpeed=1.25, followRange=16;
    int wanderChance=120;
};
inline Profile profile(const std::string& type) {
    Profile p;
    if(type=="minecraft:bat") p.locomotion=Locomotion::Bat;
    else if(type=="minecraft:cod" || type=="minecraft:salmon" || type=="minecraft:pufferfish" || type=="minecraft:tropical_fish") {p.locomotion=Locomotion::Fish;p.wanderChance=40;}
    else if(type=="minecraft:squid" || type=="minecraft:glow_squid") p.locomotion=Locomotion::Squid;
    else if(type=="minecraft:allay" || type=="minecraft:bee" || type=="minecraft:parrot" || type=="minecraft:vex" || type=="minecraft:ghast" || type=="minecraft:phantom") p.locomotion=Locomotion::Flying;
    if(type=="minecraft:zombie" || type=="minecraft:husk" || type=="minecraft:drowned" || type=="minecraft:zombie_villager") p.followRange=35;
    if(type=="minecraft:creeper" || type=="minecraft:spider" || type=="minecraft:cave_spider" || type=="minecraft:skeleton" || type=="minecraft:stray") p.wanderSpeed=.8;
    if(type=="minecraft:chicken") p.panicSpeed=1.4;
    return p;
}
// Goal starts are checked every other 20 Hz entity tick, and toGoalTicks
// converts the chance by ceil-dividing by two: no per-render-frame randomness.
inline int goalChance(int serverTicks) {return std::max(1,(serverTicks+1)/2);}
inline bool canJump(double riseBlocks,double distanceSquaredBlocks,double widthBlocks,
                    bool grounded,bool headroom,bool blockedByTallObstacle,int cooldownTicks) {
    return grounded && headroom && !blockedByTallObstacle && cooldownTicks==0
        && riseBlocks>.6 && riseBlocks<=1.25 && distanceSquaredBlocks<std::max(1.,widthBlocks);
}
inline double attackRangeBlocks() {return std::sqrt(2.04)-.6;}
inline double batComponent(double oldBlocksPerTick,double targetOffset,bool vertical) {
    const double goal=targetOffset==0 ? 0 : std::copysign(vertical ? .7 : .5,targetOffset);
    return oldBlocksPerTick+(goal-oldBlocksPerTick)*.1;
}
inline double fishSpeed(double old,double attribute,double goalMultiplier) {
    return old+(.125*(attribute*goalMultiplier-old));
}
}
