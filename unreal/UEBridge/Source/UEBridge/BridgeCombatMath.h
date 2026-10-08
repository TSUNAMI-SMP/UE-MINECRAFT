#pragma once
#include <algorithm>
#include <cmath>
#include <string>
namespace BridgeCombatMath {
struct Weapon { double damage=1, speed=4; };
inline Weapon weapon(const std::string& id) {
    if(id=="minecraft:trident") return {9,1.1};
    if(id=="minecraft:mace") return {6,.6};
    const bool wood=id.find("wooden_")!=std::string::npos,gold=id.find("golden_")!=std::string::npos;
    const bool stone=id.find("stone_")!=std::string::npos,iron=id.find("iron_")!=std::string::npos;
    const bool diamond=id.find("diamond_")!=std::string::npos,netherite=id.find("netherite_")!=std::string::npos;
    const double tier=wood||gold ? 0 : stone ? 1 : iron ? 2 : diamond ? 3 : netherite ? 4 : -1;
    if(tier<0) return {};
    if(id.find("_sword")!=std::string::npos) return {4+tier,1.6};
    if(id.find("_pickaxe")!=std::string::npos) return {2+tier,1.2};
    if(id.find("_shovel")!=std::string::npos) return {2.5+tier,1};
    if(id.find("_axe")!=std::string::npos) return {wood||gold ? 7. : netherite ? 10. : 9.,wood||stone ? .8 : iron ? .9 : 1.};
    if(id.find("_hoe")!=std::string::npos) return {1,wood||gold ? 1. : stone ? 2. : iron ? 3. : 4.};
    return {};
}
inline double charge(double seconds,double speed,double partialTicks=0) {return std::clamp((seconds*20+partialTicks)*speed/20,0.,1.);}
inline double damage(double base,double charge) {return base*(.2+.8*charge*charge);}
// Vanilla's 20-tick regeneration timer blocks equal/weaker hits for its first
// 10 ticks. A stronger hit applies only the excess without refreshing that timer.
inline double acceptedDamage(double incoming,double secondsSinceFull,double previous) {
    return secondsSinceFull<.5 ? std::max(0.,incoming-previous) : incoming;
}
inline double damping(double retentionPerTick,double seconds) {return std::pow(retentionPerTick,20*seconds);}
}
