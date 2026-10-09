"""Run the production bounded water-source policy without Unreal/GPU."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class WaterSources(unittest.TestCase):
    def test_limits_overlap_negative_positions_and_nonfinite(self):
        source = r'''
#include "BridgeWaterSourceMath.h"
#include <cassert>
#include <limits>
using namespace BridgeWaterSource;
int main() {
    std::vector<Point> sources;
    assert(CanAdd(sources,{-1250,-400,200}));
    sources.push_back({-1250,-400,200});
    assert(!CanAdd(sources,{-300,-400,200}));
    assert(CanAdd(sources,{-250,-400,200}));
    sources.push_back({-250,-400,200});
    assert(!CanAdd(sources,{9000,9000,200}));
    assert(!Valid({std::numeric_limits<double>::infinity(),0,0}));
    assert(!Valid({0,std::numeric_limits<double>::quiet_NaN(),0}));
    assert(!Valid({1e9,0,0}));
    assert(Center({1,2,3}).z==3);
    assert(Near({0,0,0},{3200,0,0},Range));
    assert(!Near({0,0,0},{3201,0,0},Range));
    assert(Resolution==128&&MaxSources==2);
}
'''
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/'water.cpp';exe=Path(d)/'water';cpp.write_text(source)
            headers=Path(__file__).resolve().parents[1]/'unreal/UEBridge/Source/UEBridge'
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I',str(headers),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
