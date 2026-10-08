"""Exercise the production reverse-face append with UE-style alias checking.

The guarded array deliberately rejects references to its own storage on Add.
This is a portable regression test, not a Windows Unreal module execution.
"""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
source = Path(os.environ.get('UEBRIDGE_MESH_SOURCE', ROOT / 'unreal/UEBridge/Source/UEBridge/BridgeBlockPreview.cpp')).read_text()
start = source.index('if(Fluid || (ItemFallback && Face.DoubleSided))')
opening = source.index('{', start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
production = source[start:end]
program = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <iostream>
using int32 = std::int32_t;
struct GuardedArray {
    std::vector<int32> values;
    int32 Num() const { return int32(values.size()); }
    const int32& operator[](int32 i) const {return values.at(i);}
    void Add(const int32& value) {
        const auto address=reinterpret_cast<std::uintptr_t>(&value);
        const auto begin=reinterpret_cast<std::uintptr_t>(values.data());
        if(address>=begin && address<begin+values.capacity()*sizeof(int32))
            throw std::runtime_error("TArray-style CheckAddress: self-aliasing Add");
        values.push_back(value);
    }
};
int main() {
    try {
        for(int flags=0;flags<8;++flags) for(int capacity:{0,6,22,64}) for(int prefix:{0,4,50}) {
            struct {GuardedArray Indices;} Section;
            Section.Indices.values.reserve(capacity);
            for(int32 i=0;i<prefix;++i) Section.Indices.Add(i);
            for(int32 i:{100,101,102,100,102,103}) Section.Indices.Add(i);
            const bool Fluid=flags&1,ItemFallback=flags&2;
            struct {bool DoubleSided;} Face{bool(flags&4)};
            std::vector<int32> expected=Section.Indices.values;
            if(Fluid || (ItemFallback && Face.DoubleSided))
                expected.insert(expected.end(),{100,102,101,100,103,102});
''' + production + r'''
            assert(Section.Indices.values==expected);
        }
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<"\n";return 2;
    }
    std::cout<<"Production reverse faces: 96 cases passed (alias guards, growth, winding, material conditions)\n";
}
'''
with tempfile.TemporaryDirectory(prefix='uebridge-mesh-append-') as temporary:
    folder=Path(temporary);cpp=folder/'check.cpp';exe=folder/'check'
    cpp.write_text(program)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
