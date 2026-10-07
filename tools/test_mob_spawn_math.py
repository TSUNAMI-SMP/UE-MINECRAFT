"""Compile portable mob placement math; no Unreal build or rendering is involved."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "BridgeMobSpawnMath.h"
#include <cassert>
#include <iostream>
int main() {
    auto candidates=BridgeMobSpawnMath::Candidates(30);
    assert(candidates.size()==17 && candidates.front()[0]==0 && candidates.front()[1]==0);
    assert(candidates[1][0]>=75 && std::abs(candidates[1][1])<1e-9);
    assert(std::abs(candidates.back()[0])<=150 && std::abs(candidates.back()[1])<=150);
    auto large=BridgeMobSpawnMath::Candidates(100);assert(large[1][0]==212);
    assert(BridgeMobSpawnMath::CapsulesOverlap(0,0,30,100,30,90));
    assert(!BridgeMobSpawnMath::CapsulesOverlap(61*61,0,30,100,30,90));
    assert(!BridgeMobSpawnMath::CapsulesOverlap(0,191,30,100,30,90));
    assert(BridgeMobSpawnMath::CapsulesOverlap(0,-189,30,100,30,90));
    std::cout << "Mob spawn math: 8 checks passed\n";
}
'''

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="bridge-mob-math-") as temporary:
        source = pathlib.Path(temporary) / "check.cpp"
        binary = pathlib.Path(temporary) / "check"
        source.write_text(SOURCE)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "unreal/UEBridge/Source/UEBridge"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
