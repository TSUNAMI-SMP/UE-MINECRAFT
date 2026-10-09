"""Compile the production constant-time index and compare it with a scan."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class VisualIndex(unittest.TestCase):
    def test_all_voxels_duplicate_shapes_and_atomic_replacement(self):
        source = r'''
#include "BridgeVoxelIndex.h"
#include <cassert>
#include <vector>
struct Row {int x,y,z,role;};
int main() {
 BridgeVoxelIndex::Cell index;
 std::vector<Row> rows;
 for(int y=0;y<8;++y) for(int z=0;z<8;++z) for(int x=0;x<8;++x) {
   rows.push_back({x,y,z,2});rows.push_back({x,y,z,1});rows.push_back({x,y,z,1});
 }
 for(std::size_t i=0;i<rows.size();++i) if(rows[i].role<=1) index.rememberFirst(rows[i].x,rows[i].y,rows[i].z,int(i));
 for(int y=0;y<8;++y) for(int z=0;z<8;++z) for(int x=0;x<8;++x) {
   int scan=-1;for(std::size_t i=0;i<rows.size();++i) if(rows[i].role<=1&&rows[i].x==x&&rows[i].y==y&&rows[i].z==z) {scan=int(i);break;}
   assert(index.find(x,y,z)==scan);
 }
 assert(index.find(-1,0,0)==-1&&index.find(8,0,0)==-1);
 index.clear();assert(index.find(4,4,4)==-1);
 index.rememberFirst(4,4,4,0);assert(index.find(4,4,4)==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'index.cpp';exe=Path(directory)/'index';cpp.write_text(source)
            headers=Path(__file__).resolve().parents[1]/'unreal/UEBridge/Source/UEBridge'
            subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(headers),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__': unittest.main()
