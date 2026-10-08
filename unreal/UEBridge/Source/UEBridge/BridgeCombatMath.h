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
    const bool stone=id.find("stone_")!=std::string::npos,copper=id.find("copper_")!=std::string::npos,iron=id.find("iron_")!=std::string::npos;
    const bool diamond=id.find("diamond_")!=std::string::npos,netherite=id.find("netherite_")!=std::string::npos;
    const double tier=wood||gold ? 0 : stone||copper ? 1 : iron ? 2 : diamond ? 3 : netherite ? 4 : -1;
    if(tier<0) return {};
    if(id.find("_sword")!=std::string::npos) return {4+tier,1.6};
    if(id.find("_pickaxe")!=std::string::npos) return {2+tier,1.2};
    if(id.find("_shovel")!=std::string::npos) return {2.5+tier,1};
    if(id.find("_axe")!=std::string::npos) return {wood||gold ? 7. : netherite ? 10. : 9.,wood||stone||copper ? .8 : iron ? .9 : 1.};
    if(id.find("_hoe")!=std::string::npos) return {1,wood||gold ? 1. : stone||copper ? 2. : iron ? 3. : 4.};
    return {};
}
// PlayerEntity#getAttackCooldownProgress receives an integer 20 Hz counter;
// the HUD passes 0, attacks pass 0.5 (not the render-frame interpolation).
inline double ticks(double seconds) {return std::floor(std::max(0.,seconds)*20+1.e-7);}
inline double charge(double seconds,double speed,double partialTicks=0) {return std::clamp((ticks(seconds)+partialTicks)*speed/20,0.,1.);}
inline double damage(double base,double charge) {return base*(.2+.8*charge*charge);}
// DamageUtil#getDamageLeft, without weapon enchantment modification.
inline double armorDamage(double amount,double armor,double toughness) {
    const double effective=std::clamp(armor-amount/(2+toughness/4),std::min(armor*.2,20.),20.);
    return amount*(1-effective/25);
}
inline double protectedDamage(double amount,double protection) {return amount*(1-std::clamp(protection,0.,20.)/25);}
// Vanilla's 20-tick regeneration timer blocks equal/weaker hits for its first
// 10 ticks. A stronger hit applies only the excess without refreshing that timer.
inline bool fullHit(double secondsSinceFull) {return ticks(secondsSinceFull)>=10;}
inline double acceptedDamage(double incoming,double secondsSinceFull,double previous) {
    return !fullHit(secondsSinceFull) ? std::max(0.,incoming-previous) : incoming;
}
inline int hurtTicks(double secondsSinceFull) {return std::max(0,10-int(ticks(secondsSinceFull)));}
struct Velocity { double x=0,y=0,z=0; };
// LivingEntity#takeKnockback, expressed in UE centimetres/second. Minecraft
// velocity is blocks/tick, so 1 velocity unit = 100 cm * 20 ticks/second.
// AwayDirection is opposite the Java source-to-target direction argument.
inline Velocity knockbackVelocity(Velocity old,double strength,double resistance,double awayX,double awayY,bool grounded) {
    strength*=1-std::clamp(resistance,0.,1.);
    if(strength<=0) return old;
    const double length=std::hypot(awayX,awayY);
    if(length<1.e-8) return old; // Caller supplies the source's random fallback.
    const double impulse=strength*2000;
    return {old.x*.5+awayX/length*impulse,old.y*.5+awayY/length*impulse,
        grounded ? std::min(800.,old.z*.5+impulse) : old.z};
}
inline double deathRoll(double seconds,double partialTicks=-1) {
    // LivingEntityRenderer uses deathTime + render tick delta (unlike charge).
    const double base=ticks(seconds);
    const double delta=partialTicks<0 ? std::clamp(std::max(0.,seconds)*20-base,0.,1.) : std::clamp(partialTicks,0.,1.);
    const double time=base+delta;
    return 90*std::min(1.,std::sqrt(std::max(0.,(time-1)/20*1.6)));
}
inline bool cooldownPassed(double charge) {return charge>.9;}
inline bool sweepAllowed(bool charged,bool critical,bool sprintKnockback,bool onGround,double horizontalMovementBlocksSquared,double movementSpeed) {
    return charged && !critical && !sprintKnockback && onGround && horizontalMovementBlocksSquared<std::pow(movementSpeed*2.5,2);
}
inline double soundPitch(double randomA,double randomB,bool baby) {return (randomA-randomB)*.2+(baby ? 1.5 : 1.);}
inline double damping(double retentionPerTick,double seconds) {return std::pow(retentionPerTick,20*seconds);}
}
