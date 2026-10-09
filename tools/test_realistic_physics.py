"""Compile/run production simulation and AVI writer; no UE/GPU claim."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ROOT / "unreal/UEBridge/Source/UEBridge"
PHYSICS = r'''
#include "BridgeRealisticPhysics.h"
#include <cassert>
#include <iostream>
using namespace BridgeRealistic;
Simulation world() {
    Simulation s;
    s.inside=[](Vec p){return p.x>=-10000&&p.x<10000&&p.y>=-10000&&p.y<10000&&p.z>=0&&p.z<10000;};
    s.blocked=[](Vec p,double r){return p.z-r<0;};
    s.sweep=[](Vec a,Vec b,double r){if(b.z<r&&a.z>=r)return Hit{true,(a.z-r)/(a.z-b.z),{0,0,1}};return Hit{};};
    s.exposure=[](Vec,Vec){return 1.;};return s;
}
int main() {
    assert(cell({-.1,-25.1,25}).x==-1&&cell({-.1,-25.1,25}).y==-2);
    auto water=world();assert(water.pour({0,0,25},Water));
    for(int i=0;i<900;++i) {water.step();assert(water.volume(Water)==BucketVolume);for(const auto& c:water.liquids) assert(c.second.amount>0&&c.second.amount<=CellCapacity);}
    auto first=water.liquids.begin()->first;assert(water.collect(center(first),Water));assert(water.volume(Water)==0);
    assert(!water.collect(center(first),Water));
    auto liquid=world();assert(liquid.pour({0,0,25},Water));assert(!liquid.pour({0,0,25},Lava));assert(liquid.volume(Water)==BucketVolume&&liquid.volume(Lava)==0);
    auto blocked=world();blocked.blocked=[](Vec,double){return true;};assert(!blocked.pour({0,0,25},Water));assert(!blocked.addSand({0,0,100}));assert(!blocked.addTnt({0,0,100}));
    auto small=world();small.liquids.emplace(Key{0,0,0},Liquid{Water,1});assert(!small.collect({1,1,1},Water));assert(small.volume(Water)==1);
    auto blast=world();assert(blast.pour({0,0,100},Water));blast.blast({0,0,50});assert(!blast.drops.empty());
    for(int i=0;i<900;++i) {blast.step();assert(blast.volume(Water)==BucketVolume);}
    auto reaction=world();reaction.liquids.emplace(Key{0,0,1},Liquid{Water,500});reaction.liquids.emplace(Key{0,0,0},Liquid{Lava,400});
    for(int i=0;i<60;++i) {reaction.step();}assert(reaction.volume(Water)+reaction.volume(Lava)+reaction.reacted==900);assert(!reaction.rocks.empty());
    auto a=world();assert(a.addSand({0,0,100}));auto b=a;
    for(int i=0;i<300;++i) {a.step();b.step();}
    assert(a.grains.size()==512);for(std::size_t i=0;i<a.grains.size();++i) {assert((a.grains[i].p-b.grains[i].p).length()==0);assert(a.grains[i].p.z>=5.49);assert(a.grains[i].p.finite());}
    // Removing support reawakens a settled pile, at most 30 simulation ticks later.
    a.sweep=[](Vec,Vec,double){return Hit{};};a.inside=[](Vec p){return p.finite();};double z=a.grains[0].p.z;for(int i=0;i<120;++i) a.step();assert(a.grains[0].p.z<z);
    auto t=world();assert(t.addTnt({0,0,100}));int explosions=0;t.exploded=[&](const Blast&){++explosions;};assert(t.ignite({-200,0,100},{1,0,0},500));
    for(int i=0;i<239;++i) {t.step();}assert(explosions==0);t.step();assert(explosions==1&&t.bombs.empty());for(int i=0;i<300;++i) {t.step();}assert(explosions==1);
    auto chain=world();assert(chain.addTnt({0,0,100}));assert(chain.addTnt({200,0,100}));int chained=0;chain.exploded=[&](const Blast&){++chained;};assert(chain.ignite({-200,0,100},{1,0,0},500));for(int i=0;i<300;++i)chain.step();assert(chained==2&&chain.bombs.empty());
    auto budget=world();budget.grains.resize(MaxGrains);assert(!budget.addSand({0,0,100}));budget.bombs.resize(MaxTnt);assert(!budget.addTnt({0,0,100}));
    auto cells=world();for(std::size_t i=0;i<MaxCells;++i) cells.rocks.emplace(Key{int(i),100,0},CellCapacity);assert(!cells.pour({0,0,100},Water));
    auto overlap=world();assert(overlap.addTnt({0,0,100}));assert(!overlap.addTnt({0,0,100}));assert(overlap.bombs.size()==1);
    auto clear=world();assert(clear.pour({0,0,25},Water));assert(clear.addTnt({500,0,100}));assert(clear.addSand({1000,0,100}));auto backup=clear;
    int removed=clear.clear(Water,{0,0,100},200);assert(removed==64&&clear.volume(Water)==0&&clear.bombs.size()==1&&clear.grains.size()==512);
    clear=backup;assert(clear.volume(Water)==BucketVolume);clear.clear(-1,{},-1);assert(clear.volume(Water)==0&&clear.grains.empty()&&clear.bombs.empty());
    auto lava=world();assert(lava.pour({0,0,100},Lava));for(int i=0;i<90;++i)lava.step();assert(lava.volume(Lava)==BucketVolume);
    std::cout<<"Production physics checks passed: finite bounds, transactions, conservation, reaction, settling, collapse, deterministic ticks, fuse, chain reaction, budgets, scoped cleanup\n";
}
'''
AVI = r'''
#include "BridgeMjpegAvi.h"
#include <fstream>
#include <iterator>
#include <cassert>
int main(int argc,char** argv) {
    assert(argc==4);std::ifstream source(argv[1],std::ios::binary);std::vector<std::uint8_t> jpeg((std::istreambuf_iterator<char>(source)),{});
    std::fstream file(argv[2],std::ios::binary|std::ios::out|std::ios::in|std::ios::trunc);
    BridgeAvi::Writer writer;writer.Write=[&](const std::uint8_t* b,std::size_t n){file.write(reinterpret_cast<const char*>(b),std::streamsize(n));return bool(file);};writer.Seek=[&](std::uint64_t p){file.seekp(std::streamoff(p));return bool(file);};
    assert(writer.begin(64,48));for(int i=0;i<120;++i)assert(writer.append(jpeg.data(),jpeg.size()));assert(writer.frames()==120);assert(writer.finish());file.close();
    std::fstream limit(argv[3],std::ios::binary|std::ios::out|std::ios::in|std::ios::trunc);BridgeAvi::Writer small;small.Limit=224+8+jpeg.size()+1+8+16;
    small.Write=[&](const std::uint8_t* b,std::size_t n){limit.write(reinterpret_cast<const char*>(b),std::streamsize(n));return bool(limit);};small.Seek=[&](std::uint64_t p){limit.seekp(std::streamoff(p));return bool(limit);};
    assert(small.begin(64,48));assert(small.append(jpeg.data(),jpeg.size()));assert(!small.append(jpeg.data(),jpeg.size()));assert(small.finish());
}
'''


class RealisticProduction(unittest.TestCase):
    def compile(self, temp, name, source):
        cpp, exe = Path(temp) / (name + ".cpp"), Path(temp) / name
        cpp.write_text(source)
        subprocess.run(["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(HEADERS), str(cpp), "-o", str(exe)], check=True)
        return exe

    def test_production_physics(self):
        with tempfile.TemporaryDirectory(prefix="bridge-realistic-test-") as temp:
            subprocess.run([str(self.compile(temp, "physics", PHYSICS))], check=True)

    def test_avi_is_indexed_decodable_exact_60fps(self):
        from PIL import Image
        with tempfile.TemporaryDirectory(prefix="bridge-avi-test-") as temp:
            directory = Path(temp)
            jpeg, avi, limited = directory / "frame.jpg", directory / "video.avi", directory / "limited.avi"
            Image.new("RGB", (64, 48), (120, 180, 220)).save(jpeg, quality=95)
            subprocess.run([str(self.compile(temp, "avi", AVI)), str(jpeg), str(avi), str(limited)], check=True)
            for path, count in ((avi, 120), (limited, 1)):
                stream = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-count_frames", "-show_streams", "-of", "json", str(path)]))["streams"][0]
                self.assertEqual(stream["r_frame_rate"], "60/1")
                self.assertEqual(int(stream["nb_read_frames"]), count)
                self.assertEqual(stream["codec_name"], "mjpeg")
                subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "null", "-"], check=True)


if __name__ == "__main__":
    unittest.main()
