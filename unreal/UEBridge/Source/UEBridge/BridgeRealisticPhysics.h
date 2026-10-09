#pragma once
// UE-independent, bounded simulation. Centimetres, fixed 60 Hz; display quality
// is deliberately absent. Fluids are conservative finite-volume cells, not SPH.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

namespace BridgeRealistic {
constexpr double StepSeconds=1.0/60, CellSize=25, Gravity=981;
constexpr int CellCapacity=1024, BucketVolume=64*CellCapacity;
constexpr std::size_t MaxGrains=8192, MaxCells=16384, MaxTnt=64, MaxDroplets=1024;
enum Kind : int {Sand=0,Water=1,Lava=2,Tnt=3,Rock=4};
struct Vec {
    double x=0,y=0,z=0;
    Vec operator+(Vec b) const {return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b) const {return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(double s) const {return {x*s,y*s,z*s};}
    double length() const {return std::sqrt(x*x+y*y+z*z);}
    bool finite() const {return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);}
};
struct Key {
    int x=0,y=0,z=0;
    bool operator<(const Key& b) const {return std::tie(x,y,z)<std::tie(b.x,b.y,b.z);}
    bool operator==(const Key& b) const {return x==b.x&&y==b.y&&z==b.z;}
};
inline Key cell(Vec p) {return {int(std::floor(p.x/CellSize)),int(std::floor(p.y/CellSize)),int(std::floor(p.z/CellSize))};}
inline Vec center(Key k) {return {(k.x+.5)*CellSize,(k.y+.5)*CellSize,(k.z+.5)*CellSize};}
struct Hit {bool hit=false;double fraction=1;Vec normal{0,0,1};};
struct Grain {Vec p,v;bool sleeping=false;};
struct Liquid {int kind=Water,amount=0;};
struct Bomb {Vec p,v;double fuse=-1;std::uint32_t id=0;};
struct Drop {Vec p,v;int kind=Water,amount=0;};
struct Blast {Vec p;double radius=600;};
class Simulation {
public:
    std::vector<Grain> grains;
    std::map<Key,Liquid> liquids;
    std::map<Key,int> rocks;
    std::vector<Bomb> bombs;
    std::vector<Drop> drops;
    std::uint64_t tick=0;
    std::uint32_t nextId=1;
    // Callbacks are runtime collision queries, never serialized.
    std::function<bool(Vec,double)> blocked;
    std::function<bool(Vec)> inside;
    std::function<Hit(Vec,Vec,double)> sweep;
    std::function<double(Vec,Vec)> exposure;
    std::function<void(const Blast&)> exploded;
    std::int64_t reacted=0;
    bool validPosition(Vec p) const {return p.finite()&&std::abs(p.x)<1e8&&std::abs(p.y)<1e8&&std::abs(p.z)<1e8&&(!inside||inside(p));}
    bool solid(Key k) const {return rocks.count(k)>0||(!validPosition(center(k)))||(blocked&&blocked(center(k),CellSize*.48));}
    std::int64_t volume(int kind) const {
        std::int64_t n=0;for(const auto& p:liquids) if(p.second.kind==kind) n+=p.second.amount;
        for(const auto& p:drops) if(p.kind==kind) n+=p.amount;
        return n;
    }
    bool pour(Vec p,int kind) {
        if((kind!=Water&&kind!=Lava)||!validPosition(p)||liquids.size()+rocks.size()+64>MaxCells) return false;
        const auto origin=cell(p);std::vector<Key> cells;
        for(int x=0;x<4;++x) for(int y=0;y<4;++y) for(int z=0;z<4;++z) {
            const Key k{origin.x+x-2,origin.y+y-2,origin.z+z};
            if(solid(k)||liquids.count(k)) return false;
            cells.push_back(k);
        }
        for(auto k:cells) liquids.emplace(k,Liquid{kind,CellCapacity});
        return true;
    }
    // Recover exactly one bucket in a connected pool, preflight before mutation.
    bool collect(Vec p,int kind) {
        Key first=cell(p);auto f=liquids.find(first);
        if(f==liquids.end()||f->second.kind!=kind) return false;
        std::vector<Key> queue{first};std::map<Key,bool> seen{{first,true}};
        std::int64_t total=0;std::size_t end=0;
        for(;end<queue.size()&&total<BucketVolume;++end) {
            auto it=liquids.find(queue[end]);total+=it->second.amount;
            for(Key d:directions()) {Key k{queue[end].x+d.x,queue[end].y+d.y,queue[end].z+d.z};
                auto n=liquids.find(k);if(n!=liquids.end()&&n->second.kind==kind&&!seen.count(k)) {seen.emplace(k,true);queue.push_back(k);}}
        }
        if(total<BucketVolume) return false;
        int remaining=BucketVolume;
        for(std::size_t i=0;i<end&&remaining>0;++i) {auto it=liquids.find(queue[i]);int n=std::min(remaining,it->second.amount);it->second.amount-=n;remaining-=n;if(it->second.amount==0) liquids.erase(it);}
        return true;
    }
    bool addSand(Vec p) {
        if(grains.size()+512>MaxGrains||!validPosition(p)) return false;
        std::vector<Grain> batch;
        for(int x=0;x<8;++x) for(int y=0;y<8;++y) for(int z=0;z<8;++z) {
            Vec q=p+Vec{(x-3.5)*12.5,(y-3.5)*12.5,(z-3.5)*12.5};
            if(!validPosition(q)||rocks.count(cell(q))||(blocked&&blocked(q,5.5))) return false;
            for(const auto& g:grains) if((g.p-q).length()<10.9) return false;
            batch.push_back({q,{},false});
        }
        grains.insert(grains.end(),batch.begin(),batch.end());return true;
    }
    bool addTnt(Vec p) {
        if(bombs.size()>=MaxTnt||nextId==0||nextId==UINT32_MAX||!validPosition(p)||rocks.count(cell(p))||(blocked&&blocked(p,48))) return false;
        for(const auto& b:bombs) if((b.p-p).length()<96) return false;
        bombs.push_back({p,{},-1,nextId++});return true;
    }
    bool ignite(Vec start,Vec direction,double reach) {
        Bomb* best=nullptr;double closest=reach;
        for(auto& b:bombs) {const Vec d=b.p-start;double along=d.x*direction.x+d.y*direction.y+d.z*direction.z;
            if(b.fuse<0&&along>=0&&along<closest&&(d-direction*along).length()<70&&(!exposure||exposure(start,b.p)>0)) {best=&b;closest=along;}}
        if(!best) return false;
        best->fuse=4;best->v.z=240;return true;
    }
    int clear(int kind,Vec p,double radius) {
        auto near=[&](Vec q){return radius<0||(p-q).length()<=radius;};int count=0;
        if(kind<0||kind==Sand) {auto end=std::remove_if(grains.begin(),grains.end(),[&](const Grain& g){if(near(g.p)){++count;return true;}return false;});grains.erase(end,grains.end());}
        if(kind<0||kind==Tnt) {auto end=std::remove_if(bombs.begin(),bombs.end(),[&](const Bomb& g){if(near(g.p)){++count;return true;}return false;});bombs.erase(end,bombs.end());}
        for(auto it=liquids.begin();it!=liquids.end();) if((kind<0||it->second.kind==kind)&&near(center(it->first))) {++count;it=liquids.erase(it);} else ++it;
        auto end=std::remove_if(drops.begin(),drops.end(),[&](const Drop& g){if((kind<0||g.kind==kind)&&near(g.p)){++count;return true;}return false;});drops.erase(end,drops.end());
        if(kind<0||kind==Rock) for(auto it=rocks.begin();it!=rocks.end();) {if(near(center(it->first))) {++count;it=rocks.erase(it);} else ++it;}
        return count;
    }
    void blast(Vec p,double radius=600) {
        auto impulse=[&](Vec q,double power) {Vec d=q-p;const double distance=std::max(1.,d.length());double e=exposure?exposure(p,q):1;
            return d*(power*std::max(0.,1-distance/radius)*e/distance)+Vec{0,0,250*std::max(0.,1-distance/radius)*e};};
        for(auto& g:grains) {g.v=g.v+impulse(g.p,1500);g.sleeping=false;}
        for(auto& b:bombs) if((b.p-p).length()<radius) {b.v=b.v+impulse(b.p,900);if(b.fuse<0) b.fuse=.25+(b.id%24)/60.;}
        for(auto it=liquids.begin();it!=liquids.end()&&drops.size()<MaxDroplets;) {
            Vec q=center(it->first),v=impulse(q,1600);
            if(v.length()>100) {const int n=std::min(it->second.amount,256);drops.push_back({q,v,it->second.kind,n});it->second.amount-=n;
                if(it->second.amount==0) {it=liquids.erase(it);continue;}}
            ++it;
        }
    }
    void step() {
        ++tick;
        std::map<Key,std::vector<std::size_t>> grid;
        for(std::size_t i=0;i<grains.size();++i) grid[cell(grains[i].p)].push_back(i);
        for(std::size_t i=0;i<grains.size();++i) {
            auto& g=grains[i];Vec prev=g.p;
            // Sleeping grains re-query support: removing a floor wakes a pile.
            if(g.sleeping) {if((tick+i)%30!=0) continue;
                Hit support=sweep?sweep(g.p,g.p+Vec{0,0,-1.5},5.5):Hit{};
                if(support.hit) continue;
                g.sleeping=false;}
            const auto fluid=liquids.find(cell(g.p));if(fluid!=liquids.end()) {g.v=g.v*.94;g.v.z+=Gravity*StepSeconds*.65;}
            g.v.z-=Gravity*StepSeconds;move(g.p,g.v,5.5,.08);
            const Key k=cell(g.p);
            for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) for(int z=-1;z<=1;++z) {
                auto near=grid.find({k.x+x,k.y+y,k.z+z});if(near==grid.end()) continue;
                for(auto j:near->second) if(j<i) {Vec d=g.p-grains[j].p;double distance=d.length();
                    if(distance>1e-5&&distance<11) {Vec normal=d*(1/distance);g.p=g.p+normal*(11-distance);double speed=g.v.x*normal.x+g.v.y*normal.y+g.v.z*normal.z;if(speed<0) g.v=g.v-normal*speed;g.v=g.v*.9;}}
            }
            if(!validPosition(g.p)) {g.p=prev;g.v={};}
            if((g.p-prev).length()<.015&&g.v.length()<2) g.sleeping=true;
        }
        std::vector<Blast> blasts;
        for(auto& b:bombs) {b.v.z-=Gravity*StepSeconds;move(b.p,b.v,48,.25);b.v.x*=.98;b.v.y*=.98;
            if(b.fuse>=0) {b.fuse=std::max(0.,b.fuse-StepSeconds);if(b.fuse<=1e-8) blasts.push_back({b.p,600});}}
        bombs.erase(std::remove_if(bombs.begin(),bombs.end(),[](const Bomb& b){return b.fuse>=0&&b.fuse<=1e-8;}),bombs.end());
        // Expired bombs removed before callbacks, so chain reactions cannot repeat them.
        for(const auto& b:blasts) {blast(b.p,b.radius);if(exploded) exploded(b);}
        for(std::size_t i=drops.size();i>0;--i) {
            auto& d=drops[i-1];d.v.z-=Gravity*StepSeconds;move(d.p,d.v,2,0);
            if(d.v.length()<30) {
                // Find room upwards after landing, never discard overflow volume.
                Key k=cell(d.p);bool placed=false;
                for(int up=0;up<12&&!placed;++up) {Key target{k.x,k.y,k.z+up};if(solid(target)) continue;
                    auto f=liquids.find(target);if(f!=liquids.end()&&f->second.kind!=d.kind) continue;
                    if(f==liquids.end()&&liquids.size()+rocks.size()>=MaxCells) continue;
                    auto& liquid=liquids[target];if(liquid.amount==0) liquid.kind=d.kind;
                    int n=std::min(d.amount,CellCapacity-liquid.amount);liquid.amount+=n;d.amount-=n;placed=d.amount==0;}
                if(placed) drops.erase(drops.begin()+std::ptrdiff_t(i-1));
            }
        }
        if(tick%3==0) flow();
    }
    static std::array<Key,6> directions() {return {{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};}
private:
    void move(Vec& p,Vec& v,double r,double bounce) {
        Vec next=p+v*StepSeconds;Hit h=sweep?sweep(p,next,r):Hit{};
        if(rocks.count(cell(next))) {h.hit=true;h.fraction=0;h.normal={0,0,1};}
        if(!h.hit&&validPosition(next)) {p=next;return;}
        if(h.hit) {p=p+(next-p)*std::clamp(h.fraction-1e-4,0.,1.);double n=v.x*h.normal.x+v.y*h.normal.y+v.z*h.normal.z;
            if(n<0) v=v-h.normal*((1+bounce)*n);
            if(h.normal.z>.5) {v.x*=.7;v.y*=.7;if(std::abs(v.z)<20) v.z=0;}}
        else v={};
    }
    void transfer(Key from,Key to,int requested) {
        auto src=liquids.find(from);if(src==liquids.end()||requested<=0||solid(to)) return;
        auto target=liquids.find(to);
        if(target==liquids.end()&&liquids.size()+rocks.size()>=MaxCells) return;
        if(target!=liquids.end()&&target->second.kind!=src->second.kind) {
            const int n=std::min({requested,src->second.amount,target->second.amount});
            // Reactions consume both fluids into a persistent solid cell; blocked
            // cells may not retain invisible liquid afterwards.
            reacted+=n*2;
            rocks[to]=target->second.amount+n;
            reacted+=target->second.amount-n;target=liquids.erase(target);
            src=liquids.find(from);src->second.amount-=n;if(src->second.amount==0) liquids.erase(src);
            return;
        }
        int room=CellCapacity-(target==liquids.end()?0:target->second.amount);
        const int n=std::min({requested,src->second.amount,room});if(n<=0) return;
        int kind=src->second.kind;src->second.amount-=n;if(src->second.amount==0) liquids.erase(src);
        auto& out=liquids[to];out.kind=kind;out.amount+=n;
    }
    void flow() {
        std::vector<Key> keys;for(const auto& p:liquids) keys.push_back(p.first);
        // Bottom up, deterministic; newly created cells move only on the next tick.
        std::sort(keys.begin(),keys.end(),[](Key a,Key b){return std::tie(a.z,a.x,a.y)<std::tie(b.z,b.x,b.y);});
        for(Key k:keys) {
            auto it=liquids.find(k);if(it==liquids.end()) continue;
            const int rate=it->second.kind==Lava?80:CellCapacity;
            transfer(k,{k.x,k.y,k.z-1},rate);
            for(int d=0;d<4;++d) {
                it=liquids.find(k);if(it==liquids.end()) break;
                const Key delta=directions()[(d+int(tick/3)%4)%4],to{k.x+delta.x,k.y+delta.y,k.z};
                auto other=liquids.find(to);const int amount=other==liquids.end()?0:other->second.amount;
                const int n=std::min(rate,(it->second.amount-amount)/4);
                if(amount==0&&it->second.amount<128) continue;
                transfer(k,to,n);
            }
        }
    }
};
} // namespace BridgeRealistic
