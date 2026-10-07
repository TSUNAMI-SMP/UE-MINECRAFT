#pragma once
#include <algorithm>
#include <cmath>

/** Capture deadlines preserve their phase across ordinary tick jitter. Resetting
 * the clock to each actual capture would turn a 30 FPS request into 21 FPS when
 * UE ticks at 42 Hz. Missed periods are discarded rather than captured in bursts. */
namespace BridgeVideoCadence {
class Scheduler {
public:
    void Reset(){Next=-1;Rate=0;}
    // Call only after readiness/backpressure checks. True consumes one capture;
    // the caller records its actual capture timestamp independently.
    bool Capture(double Now,int FramesPerSecond) {
        if(!std::isfinite(Now) || Now<0) return false;
        const int Requested=std::clamp(FramesPerSecond,1,60);
        if(Next<0 || Rate!=Requested){Rate=Requested;Next=Now;}
        constexpr double Epsilon=.000001;
        if(Now+Epsilon<Next) return false;
        const double Period=1.0/Rate;
        // An ordinary late tick keeps the scheduled phase. A stall exceeding a
        // whole period starts a fresh interval, with no backlog to replay.
        Next=Now-Next+Epsilon>=Period ? Now+Period : Next+Period;
        return true;
    }
private:
    double Next=-1;
    int Rate=0;
};
}
