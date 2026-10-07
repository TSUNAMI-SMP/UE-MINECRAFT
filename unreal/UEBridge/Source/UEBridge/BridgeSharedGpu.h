#pragma once
#include <cstdint>
#include <memory>
#include <string>

/** Optional D3D11 producer. All calls run on UE's render thread after RHI submission.
 * Three shared BGRA textures are leased until the GL consumer explicitly releases a frame.
 * JPEG remains available when the RHI/adapter/interop extension cannot share the resource. */
namespace BridgeSharedGpu {
struct Frame {
    std::uint64_t Handle=0;
    std::uint64_t Adapter=0;
    std::uint32_t Slot=0,Generation=0,Sequence=0;
};
class Producer {
public:
    Producer();
    ~Producer();
    bool Initialize(void* D3D11Device,int Width,int Height);
    bool Submit(void* D3D11Source,bool Opacity,std::uint32_t Sequence);
    bool TakeReady(Frame& Result);
    bool Failed() const;
    void Release(std::uint32_t Sequence,std::uint32_t Slot,std::uint32_t Generation);
    const std::string& Error() const;
private:
    struct State;
    std::unique_ptr<State> Impl;
};
}
