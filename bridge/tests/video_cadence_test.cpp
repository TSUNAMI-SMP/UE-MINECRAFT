#include "BridgeVideoCadence.h"
#include <iostream>
#include <stdexcept>
#include <limits>

using BridgeVideoCadence::Scheduler;
void Require(bool Valid,const char* Message){if(!Valid)throw std::runtime_error(Message);}
int Simulate(int TickHz,int Requested,double Offset=0) {
    Scheduler Clock;int Captures=0;
    for(int Tick=0;Tick<TickHz*10;++Tick) if(Clock.Capture(Offset+double(Tick)/TickHz,Requested))++Captures;
    return Captures;
}
int main() {
    int Checks=0;
    for(int TickHz:{42,60,120}) {
        Require(Simulate(TickHz,30)==300,"30 FPS cadence quantized below target on a faster UE tick");++Checks;
        Require(Simulate(TickHz,30,1'000'000)==300,"Large monotonic epoch changed capture rate");++Checks;
    }
    Require(Simulate(20,30)==200,"A slow UE tick invented unavailable captures");++Checks;
    Require(Simulate(60,60)==600,"Floating point boundary dropped exact 60 Hz captures");++Checks;
    {
        Scheduler Clock;Require(Clock.Capture(0,30),"First frame was deferred");
        Require(!Clock.Capture(0,30),"Two captures occurred in one tick");
        Require(!Clock.Capture(1.0/30-.000005,30),"Capture deadline fired early");
        Require(Clock.Capture(1.0/30,30),"Exact deadline was skipped");++Checks;
    }
    {
        Scheduler Clock;Require(Clock.Capture(0,30),"Initial capture failed");
        // No Capture calls during backpressure: no camera/render work can be queued.
        Require(Clock.Capture(2,30),"Recovered stream did not capture one current frame");
        Require(!Clock.Capture(2,30) && !Clock.Capture(2+.008,30) && !Clock.Capture(2+.02,30),"Stall replayed a capture backlog");
        Require(Clock.Capture(2+1.0/30,30),"Recovered stream lost its fresh interval");++Checks;
    }
    {
        Scheduler Clock;Require(Clock.Capture(0,60),"Initial capture failed");
        Require(Clock.Capture(.01,30),"Changed FPS did not start a new schedule");
        Require(!Clock.Capture(.02,30),"Changed schedule retained the previous faster interval");
        Clock.Reset();Require(Clock.Capture(.02,30),"New session inherited an old deadline");++Checks;
    }
    {
        Scheduler Clock;Require(!Clock.Capture(-1,30) && !Clock.Capture(std::numeric_limits<double>::quiet_NaN(),30),"Invalid time queued a capture");
        Require(Clock.Capture(0,0) && !Clock.Capture(.5,0) && Clock.Capture(1,0),"Invalid FPS was not bounded to one");++Checks;
    }
    std::cout<<"Video capture production cadence: "<<Checks<<" checks passed\n";
}
