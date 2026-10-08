#include "BridgeMovementMath.h"
#include "BridgeNativeInputMath.h"
#include <cassert>
#include <cmath>
#include <iostream>

static void near(double a,double b,double tolerance=1e-8) {assert(std::abs(a-b)<tolerance);}
int main() {
    using namespace BridgeMovementMath;
    // Independently derived keyboard/client input results from MC 1.21.11.
    const auto forward=movementInput(0,1,false);near(forward.x,0);near(forward.z,.98);
    const auto diagonal=movementInput(1,1,false);near(diagonal.x,1/std::sqrt(2.));near(diagonal.z,1/std::sqrt(2.));
    const auto sneak=movementInput(0,-1,true);near(sneak.z,-.294);
    const auto diagonalSneak=movementInput(1,1,true);near(diagonalSneak.x,.294);near(diagonalSneak.z,.294);
    const auto none=movementInput(0,0,true);near(none.x,0);near(none.z,0);
    // A 5 cm decrement retains support at a block edge. Negative travel and
    // diagonal corners follow the same X, Z, then joint collision queries.
    const auto edge=sneakMove(23,0,[](double x,double) {return x>8;});near(edge.x,8);
    const auto backwards=sneakMove(-23,0,[](double x,double) {return x<-8;});near(backwards.x,-8);
    const auto corner=sneakMove(15,15,[](double x,double z) {return x>10 || z>10 || x+z>10;});near(corner.x,5);near(corner.z,5);
    const auto small=sneakMove(2,-3,[](double,double) {return true;});near(small.x,0);near(small.z,0);
    const auto clear=sneakMove(25,-11,[](double,double) {return false;});near(clear.x,25);near(clear.z,-11);
    // Actual camera states are ticked by half, with interpolation between the
    // previous and current states. Gameplay eye height is deliberately separate.
    Eye eye;
    near(eye.update(127,.05),162);near(eye.current,144.5);
    near(eye.update(127,.025),153.25);
    near(eye.update(127,.025),144.5);near(eye.current,135.75);
    for(int fps:{30,60,120,144}) {
        Eye frames;
        double rendered=0;
        for(int i=0;i<fps;++i) rendered=frames.update(127,1./fps);
        near(frames.current,127+35*std::pow(.5,20));
        near(rendered,127+35*std::pow(.5,19));
        for(int i=0;i<fps;++i) rendered=frames.update(162,1./fps);
        assert(rendered>161.9999 && rendered<162);
    }
    // Flight mode transition retains the velocity, then the falling/flight
    // integrator acts on it. Release is gradual; it never creates a zero state.
    double v=137,p=0;
    for(int i=0;i<20;++i) {v+=98;p+=v*.05;v*=.91;}
    const auto accelerated=flight(137,98,.91,1);near(accelerated.velocity,v);near(accelerated.distance,p);
    const auto released=flight(v,0,.91,.05);near(released.velocity,v*.91);near(released.distance,v*.05);
    assert(released.velocity>0);
    near(BridgeNativeInputMath::MouseDegreesPerCount(.5),.15);
    std::cout<<"Native movement parity passed: keyboard/sneak input, straight/backward/corner edge reduction, tick-interpolated crouch eyes at 30/60/120/144 FPS, flight drag and raw mouse curve\n";
}
