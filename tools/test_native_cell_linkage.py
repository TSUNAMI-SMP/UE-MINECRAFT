"""Compile the production cell conversion in separate terrain/fluid translation units.

Minimal scalar/vector stand-ins exercise C++ linkage and negative-coordinate math.
This does not compile Unreal modules or validate Unreal container/header APIs.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UE = ROOT / 'unreal/UEBridge/Source/UEBridge'
declaration = re.search(r'static FIntVector CellOf\(const FIntVector& Block\);', (UE / 'BridgeWorld.h').read_text())
definition = re.search(r'FIntVector ABridgeWorld::CellOf\(const FIntVector& Block\)\s*\{[^}]+\}', (UE / 'BridgeWorld.cpp').read_text())
if not declaration or not definition:
    raise SystemExit('CellOf must be a shared ABridgeWorld member declared in the header')
header = """
#pragma once
#include <cmath>
#include <cstdint>
using int32 = std::int32_t;
struct FIntVector {
    int32 X,Y,Z;
    FIntVector(int32 x,int32 y,int32 z):X(x),Y(y),Z(z) {}
};
struct FMath { static int32 FloorToInt(double value) {return int32(std::floor(value));} };
class ABridgeWorld {
public:
    static FIntVector FluidCell(const FIntVector& Block);
private:
""" + declaration.group() + """
};
"""
consumer = """
#include "WorldStub.h"
#include <cassert>
FIntVector ABridgeWorld::FluidCell(const FIntVector& Block) { return CellOf(Block); }
int main() {
    for(int32 i=-1025;i<=1025;++i) {
        const int32 expected=i/8-((i<0 && i%8!=0) ? 1 : 0);
        const auto cell=ABridgeWorld::FluidCell({i,i,i});
        assert(cell.X==expected && cell.Y==expected && cell.Z==expected);
        const int32 local=i-cell.X*8;
        assert(local>=0 && local<8);
    }
}
"""
with tempfile.TemporaryDirectory(prefix='uebridge-cell-linkage-') as temporary:
    output = Path(temporary)
    (output / 'WorldStub.h').write_text(header)
    (output / 'terrain.cpp').write_text('#include "WorldStub.h"\n' + definition.group())
    (output / 'fluid.cpp').write_text(consumer)
    executable = output / 'check'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    str(output / 'terrain.cpp'), str(output / 'fluid.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('Production cell helper: cross-file linkage and 2051 negative/positive coordinate cases passed')
