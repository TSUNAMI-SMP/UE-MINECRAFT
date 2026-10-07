// Minecraft-side D3D11/OpenGL interoperability. Java calls every function on its render thread.
// No Minecraft textures/assets are stored here; only UE's live shared frame is copied on the GPU.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <GL/gl.h>
#include <jni.h>
#include <cstdint>
#include <string>
#include <unordered_map>

#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#define WGL_ACCESS_READ_ONLY_NV 0x0000
using OpenDevice=HANDLE (WINAPI*)(void*);
using CloseDevice=BOOL (WINAPI*)(HANDLE);
using Register=HANDLE (WINAPI*)(HANDLE,void*,GLuint,GLenum,GLenum);
using Unregister=BOOL (WINAPI*)(HANDLE,HANDLE);
using Lock=BOOL (WINAPI*)(HANDLE,GLint,HANDLE*);
using Unlock=BOOL (WINAPI*)(HANDLE,GLint,HANDLE*);
using Copy=void (WINAPI*)(GLuint,GLenum,GLint,GLint,GLint,GLint,GLuint,GLenum,GLint,GLint,GLint,GLint,GLsizei,GLsizei,GLsizei);
static OpenDevice Open=nullptr;static CloseDevice Close=nullptr;static Register RegisterObject=nullptr;
static Unregister UnregisterObject=nullptr;static Lock LockObjects=nullptr;static Unlock UnlockObjects=nullptr;static Copy CopyGpuImage=nullptr;
static std::string Error;
static PROC Function(const char* Name) {
    PROC Address=wglGetProcAddress(Name);auto Value=reinterpret_cast<std::uintptr_t>(Address);
    return Value<=3 || Value==std::uintptr_t(-1) ? nullptr : Address;
}
struct Texture {
    ID3D11Texture2D* Shared=nullptr;GLuint Gl=0;HANDLE Registration=nullptr;
};
struct Context {
    ID3D11Device* Device=nullptr;ID3D11DeviceContext* Commands=nullptr;HANDLE Interop=nullptr;HGLRC Owner=nullptr;
    std::unordered_map<std::uint64_t,Texture> Textures;
    ~Context() {
        // Java closes this context while the original GL context is current.
        for(auto& Pair:Textures) {
            auto& T=Pair.second;if(T.Registration && Interop) UnregisterObject(Interop,T.Registration);
            if(T.Gl) glDeleteTextures(1,&T.Gl);if(T.Shared) T.Shared->Release();
        }
        if(Interop) Close(Interop);if(Commands) Commands->Release();if(Device) Device->Release();
    }
};
static bool Probe() {
    if(!wglGetCurrentContext()){Error="No current Minecraft OpenGL context";return false;}
    Open=reinterpret_cast<OpenDevice>(Function("wglDXOpenDeviceNV"));
    Close=reinterpret_cast<CloseDevice>(Function("wglDXCloseDeviceNV"));
    RegisterObject=reinterpret_cast<Register>(Function("wglDXRegisterObjectNV"));
    UnregisterObject=reinterpret_cast<Unregister>(Function("wglDXUnregisterObjectNV"));
    LockObjects=reinterpret_cast<Lock>(Function("wglDXLockObjectsNV"));
    UnlockObjects=reinterpret_cast<Unlock>(Function("wglDXUnlockObjectsNV"));
    CopyGpuImage=reinterpret_cast<Copy>(Function("glCopyImageSubData"));
    if(!Open || !Close || !RegisterObject || !UnregisterObject || !LockObjects || !UnlockObjects || !CopyGpuImage) {
        Error="NVIDIA WGL_NV_DX_interop2 / OpenGL 4.3 unavailable";return false;
    }
    Error.clear();return true;
}
static void Fail(JNIEnv* Env,const char* Message) {
    Error=Message;Env->ThrowNew(Env->FindClass("java/lang/IllegalStateException"),Message);
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_tsunami_bridge_GpuVideoBridge_probe(JNIEnv*,jclass) {return Probe()?JNI_TRUE:JNI_FALSE;}
extern "C" JNIEXPORT jstring JNICALL Java_dev_tsunami_bridge_GpuVideoBridge_error(JNIEnv* Env,jclass) {return Env->NewStringUTF(Error.c_str());}
extern "C" JNIEXPORT jlong JNICALL Java_dev_tsunami_bridge_GpuVideoBridge_open(JNIEnv* Env,jclass,jlong AdapterId) {
    if(!Probe()){Fail(Env,Error.c_str());return 0;}
    IDXGIFactory1* Factory=nullptr;IDXGIAdapter1* Adapter=nullptr;auto* C=new Context;C->Owner=wglGetCurrentContext();
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&Factory)))) {
        delete C;Fail(Env,"Cannot enumerate DXGI adapters");return 0;
    }
    for(UINT I=0;I<64;++I) {
        if(FAILED(Factory->EnumAdapters1(I,&Adapter)))break;
        DXGI_ADAPTER_DESC1 Desc{};if(FAILED(Adapter->GetDesc1(&Desc))){Adapter->Release();Adapter=nullptr;continue;}
        std::uint64_t Id=std::uint64_t(std::uint32_t(Desc.AdapterLuid.LowPart))|(std::uint64_t(std::uint32_t(Desc.AdapterLuid.HighPart))<<32);
        if(Id==std::uint64_t(AdapterId)) break;
        Adapter->Release();Adapter=nullptr;
    }
    Factory->Release();
    if(!Adapter){delete C;Fail(Env,"UE GPU adapter was not found");return 0;}
    D3D_FEATURE_LEVEL Level{};
    HRESULT Created=D3D11CreateDevice(Adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&C->Device,&Level,&C->Commands);
    Adapter->Release();
    if(FAILED(Created)){delete C;Fail(Env,"Cannot create D3D11 consumer on UE's GPU");return 0;}
    C->Interop=Open(C->Device);
    if(!C->Interop){delete C;Fail(Env,"WGL cannot open UE GPU device; Minecraft and UE must use the same NVIDIA adapter");return 0;}
    return reinterpret_cast<std::uintptr_t>(C);
}
extern "C" JNIEXPORT void JNICALL Java_dev_tsunami_bridge_GpuVideoBridge_copy(JNIEnv* Env,jclass,jlong ContextId,jlong Handle,jint Destination,jint Width,jint Height) {
    auto* C=reinterpret_cast<Context*>(std::uintptr_t(ContextId));
    if(!C || C->Owner!=wglGetCurrentContext() || !Handle || Destination<=0 || Width<16 || Height<16 || Width>1920 || Height>1080) {
        Fail(Env,"Invalid GPU frame or render context");return;
    }
    auto It=C->Textures.find(std::uint64_t(Handle));
    if(It==C->Textures.end()) {
        Texture T;
        if(FAILED(C->Device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(Handle)),__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&T.Shared)))) {
            Fail(Env,"Cannot open UE shared D3D11 frame");return;
        }
        D3D11_TEXTURE2D_DESC Desc{};T.Shared->GetDesc(&Desc);
        if(Desc.Width!=UINT(Width) || Desc.Height!=UINT(Height) || Desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM || Desc.SampleDesc.Count!=1) {
            T.Shared->Release();Fail(Env,"Unexpected shared frame size/format");return;
        }
        glGenTextures(1,&T.Gl);T.Registration=RegisterObject(C->Interop,T.Shared,T.Gl,GL_TEXTURE_2D,WGL_ACCESS_READ_ONLY_NV);
        if(!T.Registration){glDeleteTextures(1,&T.Gl);T.Shared->Release();Fail(Env,"WGL failed to register shared BGRA frame");return;}
        It=C->Textures.emplace(std::uint64_t(Handle),T).first;
    }
    auto& T=It->second;
    if(!LockObjects(C->Interop,1,&T.Registration)){Fail(Env,"WGL failed to lock shared frame");return;}
    // Copy into Minecraft-owned immutable RGBA8 storage. MC's draw API then remains unchanged.
    for(int I=0;I<16 && glGetError()!=GL_NO_ERROR;++I){}
    CopyGpuImage(T.Gl,GL_TEXTURE_2D,0,0,0,0,GLuint(Destination),GL_TEXTURE_2D,0,0,0,0,Width,Height,1);
    GLenum CopyError=glGetError();BOOL Unlocked=UnlockObjects(C->Interop,1,&T.Registration);
    // Unlock completes GL use of the shared source before Java sends the frame lease ACK.
    if(CopyError!=GL_NO_ERROR || !Unlocked){Fail(Env,"GPU copy/unlock failed; switching to JPEG");return;}
}
extern "C" JNIEXPORT void JNICALL Java_dev_tsunami_bridge_GpuVideoBridge_close(JNIEnv* Env,jclass,jlong ContextId) {
    auto* C=reinterpret_cast<Context*>(std::uintptr_t(ContextId));if(!C) return;
    if(C->Owner!=wglGetCurrentContext()){Fail(Env,"GPU resources must close on their OpenGL render thread");return;}
    delete C;
}
