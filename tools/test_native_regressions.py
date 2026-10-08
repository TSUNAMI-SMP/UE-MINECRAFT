"""Exercise production input, winding and impulse math for the reported regressions.
No Unreal movement/rendering claim: this is portable C++ math validation only.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include "BridgeNativeInputMath.h"
#include "BridgeMeshingMath.h"
#include "BridgeCombatMath.h"
#include <cassert>
#include <cmath>
int main() {
    // Repeated downward counts stay at -90, even if UE stores that angle as 270.
    double signedPitch=0;
    for(int i=0;i<2000;i++) signedPitch=BridgeNativeInputMath::ClampPitch(signedPitch-.2);
    assert(signedPitch==-90);
    signedPitch=BridgeNativeInputMath::ClampPitch(signedPitch+.2);
    assert(std::abs(signedPitch+89.8)<1e-10);
    auto base=BridgeCombatMath::knockbackVelocity({},.4,0,1,0,true);
    assert(base.x==800 && base.z==800);
    auto sprint=BridgeCombatMath::knockbackVelocity(base,.5,0,1,0,false);
    assert(sprint.x==1400 && sprint.z==800); // sequential impulses, not deferred replacement
    // Uploaded vanilla trace's first damage tick records 0.313600006 blocks/tick.
    // With zero pre-hit vertical speed, .4 lift then -.08 gravity and .98 drag gives .3136.
    assert(std::abs((base.z/2000-.08)*.98-.3136000061035156)<1e-7);
    auto airborne=BridgeCombatMath::knockbackVelocity({0,0,-100},.4,0,1,0,false);
    assert(airborne.z==-100);
    auto immune=BridgeCombatMath::knockbackVelocity({},.4,1,1,0,true);
    assert(immune.x==0 && immune.z==0);
    // The camera ribbon has one visible triangle pair per line, not a four-wall tube.
    const std::array<BridgeMeshingMath::Point,4> ribbon={{{0,-1,0},{10,-1,0},{10,1,0},{0,1,0}}};
    const auto indices=BridgeMeshingMath::UEFacingQuad(ribbon,{0,0,1});
    assert(indices[1]==2 && indices[2]==1);
}
'''
with tempfile.TemporaryDirectory(prefix='uebridge-native-regressions-') as temp:
    cpp=Path(temp)/'check.cpp';exe=Path(temp)/'check';cpp.write_text(SOURCE)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'unreal/UEBridge/Source/UEBridge'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Signed pitch, reflected fronts, sequential knockback and recorded vanilla lift checks passed')
