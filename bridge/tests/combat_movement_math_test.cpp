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
    near(charge(.625,1.6),1);near(charge(.3125,1.6),.5);near(charge(1.25,.8),1);
    near(damage(7,0),1.4);near(damage(7,.5),2.8);near(damage(7,1),7);
    near(acceptedDamage(7,.1,7),0);near(acceptedDamage(4,.4,7),0);
    near(acceptedDamage(10,.4,7),3);near(acceptedDamage(7,.5,7),7);
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
    std::cout<<"Combat and creative flight: cooldown, stronger-hit window, 20 Hz reference and 30/60/120/144 fps parity passed\n";
}
