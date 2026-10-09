#pragma once
#include <cmath>
#include <vector>

namespace BridgeWaterSource {
constexpr unsigned MaxSources=2;
constexpr int Resolution=128;
constexpr double Width=1000,Height=800,Range=3200;
struct Point {double x=0,y=0,z=0;};
inline bool Valid(Point p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<=1e8&&std::abs(p.y)<=1e8&&std::abs(p.z)<=1e8;}
inline Point Center(Point p) {return p;}
inline bool Overlap(Point a,Point b) {return std::abs(a.x-b.x)<Width&&std::abs(a.y-b.y)<Width&&std::abs(a.z-b.z)<Height;}
inline bool CanAdd(const std::vector<Point>& sources,Point p) {
    if(!Valid(p)||sources.size()>=MaxSources) return false;
    for(auto q:sources) if(Overlap(p,q)) return false;
    return true;
}
inline bool Near(Point a,Point b,double distance) {
    const double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z<=distance*distance;
}
}
