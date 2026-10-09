#pragma once
// Minimal indexed MJPEG AVI writer. No external encoder; one newly rendered JPEG
// per 1/60 second, no frames duplicated to repair low live FPS. Silent video.
#include <cstdint>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>
#include <limits>
namespace BridgeAvi {
inline void U16(std::vector<std::uint8_t>& B,std::uint16_t V) {B.push_back(std::uint8_t(V));B.push_back(std::uint8_t(V>>8));}
inline void U32(std::vector<std::uint8_t>& B,std::uint32_t V) {for(int I=0;I<4;++I) B.push_back(std::uint8_t(V>>(I*8)));}
inline void Code(std::vector<std::uint8_t>& B,const char* S) {for(int I=0;I<4;++I) B.push_back(std::uint8_t(S[I]));}
inline std::vector<std::uint8_t> Header(int Width,int Height,std::uint32_t Frames,std::uint32_t DataBytes,std::uint32_t MaxFrame) {
    std::vector<std::uint8_t> B;
    Code(B,"RIFF");U32(B,224-8+DataBytes+8+Frames*16);Code(B,"AVI ");
    Code(B,"LIST");U32(B,192);Code(B,"hdrl");Code(B,"avih");U32(B,56);
    for(auto V:{16667u,MaxFrame*60,0u,16u,Frames,0u,1u,MaxFrame,std::uint32_t(Width),std::uint32_t(Height),0u,0u,0u,0u}) U32(B,V);
    Code(B,"LIST");U32(B,116);Code(B,"strl");Code(B,"strh");U32(B,56);Code(B,"vids");Code(B,"MJPG");
    for(auto V:{0u,0u,0u,1u,60u,0u,Frames,MaxFrame,std::numeric_limits<std::uint32_t>::max(),0u}) U32(B,V);
    U16(B,0);U16(B,0);U16(B,std::uint16_t(Width));U16(B,std::uint16_t(Height));
    Code(B,"strf");U32(B,40);U32(B,40);U32(B,std::uint32_t(Width));U32(B,std::uint32_t(Height));U16(B,1);U16(B,24);Code(B,"MJPG");U32(B,std::uint32_t(Width*Height*3));
    for(int I=0;I<4;++I) U32(B,0);
    Code(B,"LIST");U32(B,DataBytes+4);Code(B,"movi");return B;
}
class Writer {
public:
    std::function<bool(const std::uint8_t*,std::size_t)> Write;
    std::function<bool(std::uint64_t)> Seek;
    std::uint64_t Limit=1900ULL*1024*1024; // classic AVI kept below 2 GiB
    bool begin(int W,int H) {
        if(Started||!Write||!Seek||W<1||H<1||W>3840||H>2160) return false;
        Width=W;Height=H;auto B=Header(W,H,0,0,0);if(B.size()!=224||!Write(B.data(),B.size())) return false;Started=true;return true;
    }
    bool append(const std::uint8_t* Jpeg,std::size_t Size) {
        if(!Started||Failed||!Jpeg||Size<4||Size>32*1024*1024||Jpeg[0]!=255||Jpeg[1]!=216||Jpeg[Size-2]!=255||Jpeg[Size-1]!=217) return false;
        if(224+DataBytes+Size+9+8+(Entries.size()+1)*16>Limit) return false;
        std::vector<std::uint8_t> Prefix;Code(Prefix,"00dc");U32(Prefix,std::uint32_t(Size));
        if(!Write(Prefix.data(),Prefix.size())||!Write(Jpeg,Size)) {Failed=true;return false;}
        const std::uint8_t Zero=0;if((Size&1)&&!Write(&Zero,1)) {Failed=true;return false;}
        Entries.push_back({DataBytes+4,std::uint32_t(Size)});DataBytes+=std::uint32_t(8+Size+(Size&1));MaxFrame=std::max(MaxFrame,std::uint32_t(Size));return true;
    }
    bool finish() {
        if(!Started||Failed||Entries.empty()) return false;
        std::vector<std::uint8_t> Index;Code(Index,"idx1");U32(Index,std::uint32_t(Entries.size()*16));
        for(const auto& E:Entries) {Code(Index,"00dc");U32(Index,16);U32(Index,E.offset);U32(Index,E.size);}
        auto B=Header(Width,Height,std::uint32_t(Entries.size()),DataBytes,MaxFrame);
        bool Ok=Write(Index.data(),Index.size())&&Seek(0)&&Write(B.data(),B.size());Started=false;return Ok;
    }
    std::size_t frames() const {return Entries.size();}
private:
    struct Entry {std::uint32_t offset,size;};std::vector<Entry> Entries;
    int Width=0,Height=0;std::uint32_t DataBytes=0,MaxFrame=0;
    bool Started=false,Failed=false;
};
} // namespace BridgeAvi
