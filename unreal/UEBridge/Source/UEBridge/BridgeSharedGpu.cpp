#include "BridgeSharedGpu.h"
#include <array>
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#if !defined(UEBRIDGE_NATIVE_CHECK)
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#if !defined(UEBRIDGE_NATIVE_CHECK)
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace BridgeSharedGpu {
template<typename T> struct Com {
    T* P=nullptr;
    ~Com(){if(P) P->Release();}
    T** Out(){if(P){P->Release();P=nullptr;}return &P;}
    T* operator->() const{return P;}
};
static const char* Shader=R"HLSL(
Texture2D<float4> Input : register(t0);
cbuffer Config : register(b0) {uint Opacity; uint3 Padding;};
struct Vertex {float4 Position:SV_Position;};
Vertex VS(uint id:SV_VertexID) {
    Vertex v; v.Position=float4(id==2?3:-1,id==1?3:-1,0,1);return v;
}
float3 Srgb(float3 v) {
    v=saturate(v);return lerp(v*12.92,1.055*pow(max(v,0),1.0/2.4)-0.055,step(0.0031308,v));
}
float4 PS(Vertex v):SV_Target {
    float4 src=Input.Load(int3(uint2(v.Position.xy),0));
    float a=Opacity!=0?saturate(1-src.a):1;
    float3 rgb=a>0.00001?max(src.rgb,0)/a:0;
    return float4(Srgb(rgb),a);
}
)HLSL";
struct Producer::State {
    struct Slot {
        Com<ID3D11Texture2D> Texture;
        Com<ID3D11RenderTargetView> Target;
        Com<ID3D11Query> Complete;
        HANDLE Handle=nullptr;
        std::uint32_t Sequence=0;
        // 0 free, 1 GPU write pending, 2 sent/leased to the consumer.
        int Status=0;
    };
    Com<ID3D11Device> Device;
    Com<ID3D11DeviceContext> Immediate,Deferred;
    Com<ID3D11VertexShader> Vertex;
    Com<ID3D11PixelShader> Pixel;
    Com<ID3D11Buffer> Config;
    Com<ID3D11RasterizerState> Raster;
    Com<ID3D11DepthStencilState> Depth;
    Com<ID3D11BlendState> Blend;
    std::array<Slot,3> Slots;
    std::string Message="GPU sharing requires Windows D3D11";
    int Width=0,Height=0;
    std::uint32_t Generation=0;
    std::uint64_t Adapter=0;
    bool Initialized=false,Fatal=false;
};
Producer::Producer():Impl(new State){}
Producer::~Producer()=default;
const std::string& Producer::Error() const{return Impl->Message;}
bool Producer::Failed() const{return Impl->Fatal;}
bool Producer::Initialize(void* Device,int W,int H) {
    auto& S=*Impl;
    if(S.Initialized) return W==S.Width && H==S.Height;
    if(!Device || W<160 || H<90 || W>1920 || H>1080) return false;
    S.Device.P=static_cast<ID3D11Device*>(Device);S.Device.P->AddRef();
    S.Device->GetImmediateContext(S.Immediate.Out());
    auto Fail=[&](const char* Message){S.Message=Message;return false;};
    if(FAILED(S.Device->CreateDeferredContext(0,S.Deferred.Out()))) return Fail("D3D11 deferred context creation failed");
    Com<IDXGIDevice> Dxgi;Com<IDXGIAdapter> Adapter;
    DXGI_ADAPTER_DESC Desc{};
    if(FAILED(S.Device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(Dxgi.Out()))) || FAILED(Dxgi->GetAdapter(Adapter.Out()))
        || FAILED(Adapter->GetDesc(&Desc))) return Fail("D3D11 adapter identity unavailable");
    S.Adapter=std::uint64_t(std::uint32_t(Desc.AdapterLuid.LowPart))|(std::uint64_t(std::uint32_t(Desc.AdapterLuid.HighPart))<<32);
    Com<ID3DBlob> VS,PS,Errors;
    if(FAILED(D3DCompile(Shader,std::strlen(Shader),nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,VS.Out(),Errors.Out()))
        || FAILED(D3DCompile(Shader,std::strlen(Shader),nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,PS.Out(),Errors.Out())))
        return Fail("D3D11 shared texture color shader failed to compile");
    if(FAILED(S.Device->CreateVertexShader(VS->GetBufferPointer(),VS->GetBufferSize(),nullptr,S.Vertex.Out()))
        || FAILED(S.Device->CreatePixelShader(PS->GetBufferPointer(),PS->GetBufferSize(),nullptr,S.Pixel.Out()))) return Fail("D3D11 shader creation failed");
    D3D11_BUFFER_DESC Buffer{};Buffer.ByteWidth=16;Buffer.Usage=D3D11_USAGE_DEFAULT;Buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(S.Device->CreateBuffer(&Buffer,nullptr,S.Config.Out()))) return Fail("D3D11 conversion constants unavailable");
    D3D11_RASTERIZER_DESC Raster{};Raster.FillMode=D3D11_FILL_SOLID;Raster.CullMode=D3D11_CULL_NONE;Raster.DepthClipEnable=1;
    D3D11_DEPTH_STENCIL_DESC Depth{};Depth.DepthEnable=0;
    D3D11_BLEND_DESC Blend{};Blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(S.Device->CreateRasterizerState(&Raster,S.Raster.Out())) || FAILED(S.Device->CreateDepthStencilState(&Depth,S.Depth.Out()))
        || FAILED(S.Device->CreateBlendState(&Blend,S.Blend.Out()))) return Fail("D3D11 conversion state creation failed");
    static std::uint32_t NextGeneration=0;
    S.Generation=++NextGeneration;S.Width=W;S.Height=H;
    for(auto& Slot:S.Slots) {
        D3D11_TEXTURE2D_DESC Texture{};Texture.Width=W;Texture.Height=H;Texture.MipLevels=1;Texture.ArraySize=1;
        Texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;Texture.SampleDesc.Count=1;Texture.Usage=D3D11_USAGE_DEFAULT;
        Texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;Texture.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        D3D11_QUERY_DESC Query{};Query.Query=D3D11_QUERY_EVENT;
        Com<IDXGIResource> Shared;
        if(FAILED(S.Device->CreateTexture2D(&Texture,nullptr,Slot.Texture.Out()))
            || FAILED(S.Device->CreateRenderTargetView(Slot.Texture.P,nullptr,Slot.Target.Out()))
            || FAILED(S.Device->CreateQuery(&Query,Slot.Complete.Out()))
            || FAILED(Slot.Texture->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(Shared.Out())))
            || FAILED(Shared->GetSharedHandle(&Slot.Handle))) return Fail("D3D11 shared texture creation failed");
    }
    S.Initialized=true;S.Message="D3D11 shared BGRA8 / single sRGB conversion";return true;
}
bool Producer::Submit(void* Source,bool Opacity,std::uint32_t Sequence) {
    auto& S=*Impl;if(!S.Initialized || !Source) return false;
    State::Slot* Free=nullptr;for(auto& Slot:S.Slots) if(Slot.Status==0){Free=&Slot;break;}
    if(!Free) return false;
    Com<ID3D11ShaderResourceView> Input;
    if(FAILED(S.Device->CreateShaderResourceView(static_cast<ID3D11Resource*>(Source),nullptr,Input.Out()))) {
        S.Message="D3D11 capture texture cannot be sampled";S.Fatal=true;return false;
    }
    const std::uint32_t Config[4]={Opacity?1u:0u,0,0,0};
    S.Deferred->UpdateSubresource(S.Config.P,0,nullptr,Config,0,0);
    D3D11_VIEWPORT View{};View.Width=float(S.Width);View.Height=float(S.Height);View.MaxDepth=1;
    S.Deferred->RSSetViewports(1,&View);S.Deferred->RSSetState(S.Raster.P);
    S.Deferred->OMSetDepthStencilState(S.Depth.P,0);S.Deferred->OMSetBlendState(S.Blend.P,nullptr,~0u);
    S.Deferred->OMSetRenderTargets(1,&Free->Target.P,nullptr);
    S.Deferred->IASetInputLayout(nullptr);S.Deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    S.Deferred->VSSetShader(S.Vertex.P,nullptr,0);S.Deferred->PSSetShader(S.Pixel.P,nullptr,0);
    S.Deferred->GSSetShader(nullptr,nullptr,0);S.Deferred->HSSetShader(nullptr,nullptr,0);S.Deferred->DSSetShader(nullptr,nullptr,0);
    S.Deferred->PSSetShaderResources(0,1,&Input.P);S.Deferred->PSSetConstantBuffers(0,1,&S.Config.P);S.Deferred->Draw(3,0);
    ID3D11ShaderResourceView* Null=nullptr;S.Deferred->PSSetShaderResources(0,1,&Null);
    S.Deferred->End(Free->Complete.P);
    Com<ID3D11CommandList> Commands;
    if(FAILED(S.Deferred->FinishCommandList(0,Commands.Out()))) {S.Message="D3D11 conversion command list failed";S.Fatal=true;return false;}
    // TRUE restores every native state: UE's cached RHI state must stay valid.
    S.Immediate->ExecuteCommandList(Commands.P,1);S.Immediate->Flush();
    Free->Sequence=Sequence;Free->Status=1;return true;
}
bool Producer::TakeReady(Frame& Result) {
    auto& S=*Impl;if(!S.Initialized) return false;
    for(std::uint32_t I=0;I<S.Slots.size();++I) {
        auto& Slot=S.Slots[I];BOOL Complete=0;
        if(Slot.Status!=1)continue;
        const HRESULT Status=S.Immediate->GetData(Slot.Complete.P,&Complete,sizeof(Complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(FAILED(Status)){S.Fatal=true;S.Message="D3D11 GPU completion/device failed";return false;}
        if(Status==S_OK && Complete) {
            Slot.Status=2;Result.Handle=reinterpret_cast<std::uintptr_t>(Slot.Handle);Result.Adapter=S.Adapter;
            Result.Slot=I;Result.Generation=S.Generation;Result.Sequence=Slot.Sequence;return true;
        }
    }
    return false;
}
void Producer::Release(std::uint32_t Sequence,std::uint32_t Slot,std::uint32_t Generation) {
    auto& S=*Impl;if(S.Initialized && Slot<S.Slots.size() && Generation==S.Generation
        && S.Slots[Slot].Status==2 && S.Slots[Slot].Sequence==Sequence) S.Slots[Slot].Status=0;
}
}
#else
namespace BridgeSharedGpu {
struct Producer::State {std::string Message="GPU sharing requires Windows D3D11";};
Producer::Producer():Impl(new State){} Producer::~Producer()=default;
bool Producer::Initialize(void*,int,int){return false;} bool Producer::Submit(void*,bool,std::uint32_t){return false;}
bool Producer::TakeReady(Frame&){return false;} void Producer::Release(std::uint32_t,std::uint32_t,std::uint32_t){}
bool Producer::Failed() const{return true;}
const std::string& Producer::Error() const{return Impl->Message;}
}
#endif
