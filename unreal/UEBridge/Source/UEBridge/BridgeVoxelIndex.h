#pragma once
#include <array>
#include <cstdint>
namespace BridgeVoxelIndex {
// Indices survive TArray reallocation; rebuilt before queries after each edit.
struct Cell {
    std::array<std::int32_t,512> rows;
    Cell() {clear();}
    void clear() {rows.fill(-1);}
    static int slot(int x,int y,int z) {return x<0||x>=8||y<0||y>=8||z<0||z>=8?-1:x+(z<<3)+(y<<6);}
    void rememberFirst(int x,int y,int z,std::int32_t row) {const int s=slot(x,y,z);if(s>=0&&rows[std::size_t(s)]<0) rows[std::size_t(s)]=row;}
    std::int32_t find(int x,int y,int z) const {const int s=slot(x,y,z);return s<0?-1:rows[std::size_t(s)];}
};
}
