"""Exercise the production legacy falling age/boundary recovery policy."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class FallingState(unittest.TestCase):
    def test_expired_saved_entities_and_boundary_recovery(self):
        source=r'''
#include "BridgeFallingStateMath.h"
#include <cassert>
#include <initializer_list>
#include <limits>
using namespace BridgeFallingState;
int main() {
    int age=0;
    for(int saved:{604,610,611,612,613,614,615,616,617,618,619}) {
        assert(RestoreAge(saved,age)&&age==601);
        for(int tick=0;tick<100000;++tick) age=AdvanceAge(age);
        assert(age==601);
    }
    assert(RestoreAge(600,age)&&age==600&&AdvanceAge(age)==601);
    assert(RestoreAge(0,age)&&age==0);
    assert(RestoreAge(std::numeric_limits<int>::max(),age)&&age==601);
    for(double invalid:{-1.,600.5,2147483648.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        age=12;assert(!RestoreAge(invalid,age)&&age==12);
    }
    assert(RecoverBelowBoundary(-49.833977277487392,16,601,true));
    assert(RecoverBelowBoundary(-49.648318108513337,16,601,true));
    assert(!RecoverBelowBoundary(23.643089369875206,16,601,true));
    assert(!RecoverBelowBoundary(-49.8,16,600,true));
    assert(!RecoverBelowBoundary(-49.8,16,601,false));
    assert(!RecoverBelowBoundary(std::numeric_limits<double>::quiet_NaN(),16,601,true));
}
'''
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/'falling.cpp';exe=Path(d)/'falling';cpp.write_text(source)
            headers=Path(__file__).resolve().parents[1]/'unreal/UEBridge/Source/UEBridge'
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I',str(headers),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
