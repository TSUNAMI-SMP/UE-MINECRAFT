#include "BridgeVideoMask.h"
#include <vector>
#include <iostream>
#include <stdexcept>

int main() {
    int Passed=0;
    for(std::size_t Size:{0u,1u,65535u,65536u,1920u*1080u}) for(int Mode=0;Mode<3;++Mode) {
        std::vector<std::uint8_t> Pixels(Size),Bytes,Decoded;
        for(std::size_t I=0;I<Size;++I) Pixels[I]=Mode==0 ? 255 : Mode==1 ? std::uint8_t(I%2 ? 128 : 0) : std::uint8_t((I/300)%256);
        BridgeVideoMask::Encode(Pixels.data(),Pixels.size(),[&](std::uint8_t Byte){Bytes.push_back(Byte);});
        if(Bytes.size()>Pixels.size()*3 || Bytes.size()%3) throw std::runtime_error("Run stream exceeds bounded wire payload");
        for(std::size_t I=0;I<Bytes.size();I+=3) {
            const unsigned Count=(unsigned(Bytes[I])<<8)|Bytes[I+1];
            if(!Count || Decoded.size()+Count>Size) throw std::runtime_error("Zero or overflowing run");
            Decoded.insert(Decoded.end(),Count,Bytes[I+2]);
        }
        if(Decoded!=Pixels) throw std::runtime_error("Opacity mask differs from captured pixels");
        if(Mode==0 && Size==65536 && Bytes!=std::vector<std::uint8_t>{255,255,255,0,1,255})
            throw std::runtime_error("65536 pixel run does not split exactly at65535");
        ++Passed;
    }
    std::cout<<"Video mask production encoder: "<<Passed<<" lossless round trips passed\n";
}
