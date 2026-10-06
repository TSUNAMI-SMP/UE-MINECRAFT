#pragma once
#include <cstddef>
#include <cstdint>

namespace BridgeVideoMask {
/** Wire v3: a big-endian16-bit nonzero run length followed by one lossless opacity byte. */
template<class Emit> void Encode(const std::uint8_t* Pixels,std::size_t Size,Emit Write) {
    for(std::size_t I=0;I<Size;) {
        const std::uint8_t Value=Pixels[I];std::uint16_t Count=1;
        while(I+Count<Size && Count<65535 && Pixels[I+Count]==Value) ++Count;
        Write(std::uint8_t(Count>>8));Write(std::uint8_t(Count));Write(Value);I+=Count;
    }
}
}
