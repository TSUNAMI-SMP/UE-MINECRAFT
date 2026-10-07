#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <vector>

/** Bounded, incremental two-channel voxel lighting. Coordinates use Minecraft axes. */
namespace BridgeLightingMath {
struct Voxel { int x=0,y=0,z=0; };
struct Light { uint8_t sky=15,block=0; };
struct Environment {
    float skyFactor=1,blockFactor=1.5f,ambient=0,gamma=.5f,nightVision=0,darkness=0,darkenWorld=0;
    std::array<float,3> skyColor{{1,1,1}},ambientColor{{1,1,1}};
};
inline float Brightness(float level) { level=std::clamp(level,0.f,1.f);return level/(4.f-3.f*level); }
/** MC 1.21.11 lightmap transfer, represented as linear output for UE's sRGB presentation. */
inline std::array<float,3> Lightmap(float sky,float block,const Environment& e) {
    const float b=Brightness(block)*e.blockFactor,s=Brightness(sky)*e.skyFactor;
    std::array<float,3> c{{b,b*((b*.6f+.4f)*.6f+.4f),b*(b*b*.6f+.4f)}};
    for(int i=0;i<3;++i) c[i]=(c[i]*(1-e.ambient)+e.ambientColor[i]*e.ambient+e.skyColor[i]*s)*.96f+.03f;
    if(e.ambient<=0) for(int i=0;i<3;++i) c[i]*=1-e.darkenWorld*(i==0?.3f:.4f);
    const float maximum=std::max({c[0],c[1],c[2],.00001f});
    if(e.nightVision>0 && maximum<1) for(auto& v:c) v=v*(1-e.nightVision)+v/maximum*e.nightVision;
    if(e.ambient<=0) for(auto& v:c) v-=e.darkness;
    for(auto& v:c) v=std::clamp(v,0.f,1.f);
    const float max2=std::max({c[0],c[1],c[2],.00001f});
    const float factor=(1-std::pow(1-max2,4))/max2;
    for(auto& v:c) {
        v=(v*(1-e.gamma)+v*factor*e.gamma)*.96f+.03f;
        // Minecraft multiplies its texture RGB by a lightmap in display colour space.
        // UE textures arrive linear; convert the multiplier so final encode is performed once.
        v=v<=.04045f ? v/12.92f : std::pow((v+.055f)/1.055f,2.4f);
    }
    return c;
}
inline float FaceShade(float nx,float ny,float nz) {
    const float total=std::abs(nx)+std::abs(ny)+std::abs(nz);
    return total<.00001f ? 1.f : (std::abs(nx)*.6f+std::abs(nz)*.8f+std::abs(ny)*(ny>=0?1.f:.5f))/total;
}
inline float AO(bool sideA,bool sideB,bool corner) {
    // Vanilla averages the exposed face (1) and three occluder AO samples (.2).
    // When both sides occlude, the diagonal reuses a side sample: (1+.2+.2+.2)/4.
    // The old .2 shortcut applied an extra full occluder and made corners too dark.
    if(sideA && sideB) return .4f;
    return 1.f-.2f*(static_cast<int>(sideA)+static_cast<int>(sideB)+static_cast<int>(corner));
}
/** Vanilla substitutes the face light for a completely zero packed neighboring sample. */
inline std::array<float,2> CornerLight(Light face,Light sideA,Light sideB,Light corner) {
    float sky=face.sky,block=face.block;
    for(auto sample:{sideA,sideB,corner}) {
        if(sample.sky==0 && sample.block==0) sample=face;
        sky+=sample.sky;block+=sample.block;
    }
    return {sky/60.f,block/60.f};
}
class Field {
public:
    static constexpr size_t MaxVoxels=8*1024*1024;
    bool reset(Voxel minimum,Voxel maximum,bool hasSky=true) {
        min=minimum;sx=maximum.x-min.x+1;sy=maximum.y-min.y+1;sz=maximum.z-min.z+1;
        const int64_t volume=static_cast<int64_t>(sx)*sy*sz;
        if(sx<=0 || sy<=0 || sz<=0 || sx>256 || sy>256 || sz>256 || volume>static_cast<int64_t>(MaxVoxels)) {clear();return false;}
        skyEnabled=hasSky;opacity.assign(volume,0);faces.assign(volume,0);emission.assign(volume,0);sky.assign(volume,0);block.assign(volume,0);direct.assign(volume,0);queued.assign(volume,0);
        top.assign(static_cast<size_t>(sx)*sz,hasSky?15:0);boundaryDefined.assign(top.size(),0);queue.clear();changed.clear();initialized=false;initializing=false;columnCursor=0;return true;
    }
    void clear() {sx=sy=sz=0;opacity.clear();faces.clear();emission.clear();sky.clear();block.clear();direct.clear();queued.clear();top.clear();boundaryDefined.clear();queue.clear();changed.clear();initialized=false;initializing=false;columnCursor=0;}
    bool valid() const {return sx>0;}
    Voxel minimum() const {return min;}
    Voxel maximum() const {return {min.x+sx-1,min.y+sy-1,min.z+sz-1};}
    size_t voxelCount() const {return opacity.size();}
    bool ready() const {return initialized && !initializing;}
    bool inside(Voxel p) const {return p.x>=min.x && p.y>=min.y && p.z>=min.z && p.x<min.x+sx && p.y<min.y+sy && p.z<min.z+sz;}
    void set(Voxel p,uint8_t occlusion,uint8_t luminous,uint8_t solidFaces=0) {
        if(!inside(p)) return;
        const size_t i=index(p);occlusion=std::min<uint8_t>(occlusion,15);luminous=std::min<uint8_t>(luminous,15);
        solidFaces&=63;
        if(opacity[i]==occlusion && emission[i]==luminous && faces[i]==solidFaces) return;
        opacity[i]=occlusion;emission[i]=luminous;faces[i]=solidFaces;
        if(columnReady(p.x,p.z)) {column(p.x,p.z);enqueue(i);neighbors(p,[this](size_t n){enqueue(n);});}
    }
    void setBoundary(int x,int z,uint8_t value) {
        if(x<min.x || x>=min.x+sx || z<min.z || z>=min.z+sz) return;
        const size_t i=static_cast<size_t>(z-min.z)*sx+(x-min.x);value=std::min<uint8_t>(value,15);
        boundaryDefined[i]=1;
        if(top[i]!=value) {top[i]=value;if(columnReady(x,z)) column(x,z);}
        // Extrapolate the one-voxel mesh sampling margin, until its own native cap arrives.
        for(int dx=-1;dx<=1;++dx) for(int dz=-1;dz<=1;++dz) {
            const int xx=x+dx,zz=z+dz;
            if(xx<min.x || xx>=min.x+sx || zz<min.z || zz>=min.z+sz) continue;
            const size_t n=static_cast<size_t>(zz-min.z)*sx+(xx-min.x);
            if(!boundaryDefined[n] && top[n]!=value) {top[n]=value;if(columnReady(xx,zz)) column(xx,zz);}
        }
    }
    void seed(Voxel p,uint8_t skyValue,uint8_t blockValue) {
        if(!inside(p) || initialized) return;
        const size_t i=index(p);skyValue=std::min<uint8_t>(skyValue,15);blockValue=std::min<uint8_t>(blockValue,15);
        if(sky[i]!=skyValue || block[i]!=blockValue) {sky[i]=skyValue;block[i]=blockValue;enqueue(i);}
    }
    /** Direct columns are scanned once; diffuse propagation is then time-budgeted by step(). */
    void initialize() {
        if(!valid()) return;
        initializing=false;
        for(int z=min.z;z<min.z+sz;++z) for(int x=min.x;x<min.x+sx;++x) column(x,z);
        initialized=true;
        for(size_t i=0;i<emission.size();++i) if(emission[i]) enqueue(i);
    }
    void beginInitialize() {if(valid()) {initializing=true;initialized=false;columnCursor=0;}}
    size_t step(size_t budget) {
        size_t work=0;
        while(initializing && columnCursor<static_cast<size_t>(sx)*sz && work<budget) {
            const int x=min.x+static_cast<int>(columnCursor%sx),z=min.z+static_cast<int>(columnCursor/sx);
            column(x,z);++columnCursor;work+=sy;
        }
        if(initializing && columnCursor>=static_cast<size_t>(sx)*sz) {
            initializing=false;initialized=true;
            for(size_t i=0;i<emission.size();++i) if(emission[i]) enqueue(i);
        }
        while(!initializing && !queue.empty() && work<budget) {
            ++work;const size_t i=queue.front();queue.pop_front();queued[i]=0;const Voxel p=position(i);
            uint8_t newSky=direct[i],newBlock=emission[i];
            const uint8_t attenuation=std::max<uint8_t>(1,opacity[i]);
            if(opacity[i]<15) neighborsDirections(p,[&](size_t n,int side) {
                if(!(faces[i]&(1<<side)) && !(faces[n]&(1<<(side^1))))
                    newSky=std::max<uint8_t>(newSky,sky[n]>attenuation?sky[n]-attenuation:0);
                if(!(faces[i]&(1<<side)) && (!(faces[n]&(1<<(side^1))) || emission[n]))
                    newBlock=std::max<uint8_t>(newBlock,block[n]>attenuation?block[n]-attenuation:0);
            });
            if(sky[i]!=newSky || block[i]!=newBlock) {
                sky[i]=newSky;block[i]=newBlock;changed.push_back(p);
                neighbors(p,[this](size_t n){enqueue(n);});
            }
        }
        return work;
    }
    size_t pending() const {return queue.size()+(initializing?(static_cast<size_t>(sx)*sz-columnCursor)*sy:0);}
    Light sample(Voxel p) const {
        if(!inside(p)) return {static_cast<uint8_t>(skyEnabled?15:0),0};
        const size_t i=index(p);return {sky[i],block[i]};
    }
    bool opaque(Voxel p) const {return inside(p) && opacity[index(p)]>=15;}
    uint8_t opacityAt(Voxel p) const {return inside(p)?opacity[index(p)]:0;}
    uint8_t emissionAt(Voxel p) const {return inside(p)?emission[index(p)]:0;}
    void discardChanged() {changed.clear();}
    std::vector<Voxel> consumeChanged() {std::vector<Voxel> result;result.swap(changed);return result;}
private:
    Voxel min;int sx=0,sy=0,sz=0;bool initialized=false,initializing=false,skyEnabled=true;size_t columnCursor=0;
    std::vector<uint8_t> opacity,faces,emission,sky,block,direct,queued,top,boundaryDefined;
    std::deque<size_t> queue;std::vector<Voxel> changed;
    size_t index(Voxel p) const {return static_cast<size_t>((p.y-min.y)*sz+(p.z-min.z))*sx+(p.x-min.x);}
    Voxel position(size_t i) const {const int x=i%sx;i/=sx;const int z=i%sz;return {min.x+x,min.y+static_cast<int>(i/sz),min.z+z};}
    void enqueue(size_t i) {if(!queued[i]) {queued[i]=1;queue.push_back(i);}}
    template<class F> void neighbors(Voxel p,F action) const {
        const Voxel n[6]={{p.x-1,p.y,p.z},{p.x+1,p.y,p.z},{p.x,p.y-1,p.z},{p.x,p.y+1,p.z},{p.x,p.y,p.z-1},{p.x,p.y,p.z+1}};
        for(auto v:n) if(inside(v)) action(index(v));
    }
    template<class F> void neighborsDirections(Voxel p,F action) const {
        const Voxel n[6]={{p.x-1,p.y,p.z},{p.x+1,p.y,p.z},{p.x,p.y-1,p.z},{p.x,p.y+1,p.z},{p.x,p.y,p.z-1},{p.x,p.y,p.z+1}};
        for(int side=0;side<6;++side) if(inside(n[side])) action(index(n[side]),side);
    }
    bool columnReady(int x,int z) const {return initialized || (initializing && static_cast<size_t>(z-min.z)*sx+(x-min.x)<columnCursor);}
    void column(int x,int z) {
        uint8_t sunlight=skyEnabled?top[static_cast<size_t>(z-min.z)*sx+(x-min.x)]:0;
        bool aboveDown=false;
        for(int y=min.y+sy-1;y>=min.y;--y) {
            const size_t i=index({x,y,z});
            if(aboveDown || (faces[i]&(1<<3))) sunlight=0;
            aboveDown=(faces[i]&(1<<2))!=0;
            const uint8_t loss=sunlight==15?opacity[i]:std::max<uint8_t>(1,opacity[i]);
            sunlight=sunlight>loss?sunlight-loss:0;
            if(direct[i]!=sunlight) {
                direct[i]=sunlight;
                if(!initialized) {sky[i]=sunlight;if(initializing) changed.push_back({x,y,z});}
                enqueue(i);
            }
        }
    }
};
}
