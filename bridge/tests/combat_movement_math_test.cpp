#include "BridgeCombatMath.h"
#include "BridgeMovementMath.h"
#include <cassert>
#include <iostream>
#include <cmath>
static void near(double a,double b,double epsilon=1e-8) {assert(std::abs(a-b)<epsilon);}
int main() {
    using namespace BridgeCombatMath;
    near(weapon("minecraft:diamond_sword").damage,7);near(weapon("minecraft:diamond_sword").speed,1.6);
    near(weapon("minecraft:wooden_axe").damage,7);near(weapon("minecraft:netherite_axe").damage,10);
    near(weapon("minecraft:copper_sword").damage,5);near(weapon("minecraft:copper_axe").damage,9);near(weapon("minecraft:copper_axe").speed,.8);
    near(charge(.625,1.6),.96);near(charge(.65,1.6),1);near(charge(.3125,1.6),.48);near(charge(.3,1.6,.5),.52);near(charge(1.25,.8),1);
    // A render frame may pass through the threshold between simulation ticks:
    // neither the HUD nor attack charge may use that fractional frame time.
    near(charge(.62499,1.6),.96);near(charge(.64999,1.6),.96);
    near(damage(7,0),1.4);near(damage(7,.5),2.8);near(damage(7,1),7);
    near(armorDamage(7,2,0),6.888); // Zombie's natural armor 2 remains at its .4 minimum.
    near(armorDamage(10,20,8),3);near(armorDamage(10,0,0),10);
    near(protectedDamage(10,20),2);near(protectedDamage(10,100),2);
    near(acceptedDamage(7,.1,7),0);near(acceptedDamage(4,.4,7),0);
    near(acceptedDamage(10,.4,7),3);near(acceptedDamage(7,.5,7),7);
    near(acceptedDamage(10,.4999,7),3);near(acceptedDamage(10,.5,7),10);
    assert(hurtTicks(0)==10 && hurtTicks(.0499)==10 && hurtTicks(.05)==9 && hurtTicks(.5)==0);
    // Independent values from LivingEntity.takeKnockback: old velocity halves
    // only horizontally; vertical impulse applies only while grounded.
    const auto ground=knockbackVelocity({200,-400,-200},.4,0,3,4,true);
    near(ground.x,580);near(ground.y,440);near(ground.z,700);
    const auto air=knockbackVelocity({200,-400,-200},.4,0,3,4,false);
    near(air.x,580);near(air.y,440);near(air.z,-200);
    const auto resisted=knockbackVelocity({200,-400,1000},.4,.5,3,4,true);
    near(resisted.x,340);near(resisted.y,120);near(resisted.z,800);
    const auto immune=knockbackVelocity({200,-400,1000},.4,1,3,4,true);
    near(immune.x,200);near(immune.y,-400);near(immune.z,1000);
    // Full charged sprint gives two source calls, .4 base then .5 bonus.
    const auto sprint=knockbackVelocity(knockbackVelocity({},.4,0,1,0,true),.5,0,1,0,true);
    near(sprint.x,1400);near(sprint.y,0);near(sprint.z,800);
    const auto resistantSprint=knockbackVelocity(knockbackVelocity({},.4,.5,1,0,true),.5,.5,1,0,true);
    near(resistantSprint.x,700);near(resistantSprint.z,700);
    near(deathRoll(.05),0);near(deathRoll(.3),90*std::sqrt(.4));near(deathRoll(.7),90);
    near(deathRoll(.075),18); // Death renderer interpolates; charge HUD does not.
    near(soundPitch(1,0,false),1.2);near(soundPitch(0,1,true),1.3);
    assert(!cooldownPassed(.9) && cooldownPassed(.9001));
    assert(sweepAllowed(true,false,false,true,.0624,.1));
    assert(!sweepAllowed(true,false,false,true,.0625,.1));
    assert(!sweepAllowed(true,true,false,true,0,.1));
    assert(!sweepAllowed(true,false,true,true,0,.1));
    // Independent Minecraft reference: accelerate, move, then apply tick drag.
    for(double retention:{.91,.6}) for(double impulse:{0.,98.,196.,300.}) {
        double velocity=137,position=0;
        for(int tick=0;tick<40;++tick) {velocity+=impulse;position+=velocity*.05;velocity*=retention;}
        const auto whole=BridgeMovementMath::flight(137,impulse,retention,2);
        near(whole.velocity,velocity);near(whole.distance,position);
        for(int fps:{30,60,120,144}) {
            double v=137,p=0;
            for(int frame=0;frame<2*fps;++frame) {const auto step=BridgeMovementMath::flight(v,impulse,retention,1./fps);v=step.velocity;p+=step.distance;}
            near(v,velocity);near(p,position);
        }
    }
    const auto released=BridgeMovementMath::flight(990.8888888889,0,.91,.05);
    assert(released.velocity>900 && released.distance>49); // release retains momentum.
    std::cout<<"Combat: discrete charge/hurt ticks, stronger-hit window, source knockback/death/sweep/sound and flight frame parity passed\n";
}
