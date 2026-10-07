#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

// Native VoxelShape boxes can share faces. Emit only the boundary's actual creases.
namespace BridgeOutlineMath {
using Point=std::array<double,3>;
struct Box { Point Min,Max; };
struct Line { Point A,B; };
inline bool Inside(const std::vector<Box>& Boxes,const Point& P) {
    for(const auto& B:Boxes) if(P[0]>B.Min[0] && P[0]<B.Max[0] && P[1]>B.Min[1] && P[1]<B.Max[1] && P[2]>B.Min[2] && P[2]<B.Max[2]) return true;
    return false;
}
inline std::vector<Line> Edges(const std::vector<Box>& Boxes) {
    std::vector<Line> Result;
    for(const auto& B:Boxes) for(int Axis=0;Axis<3;++Axis) for(int U=0;U<2;++U) for(int V=0;V<2;++V) {
        const int J=(Axis+1)%3,K=(Axis+2)%3;
        Point P=B.Min;P[J]=U ? B.Max[J] : B.Min[J];P[K]=V ? B.Max[K] : B.Min[K];
        std::vector<double> Cuts{B.Min[Axis],B.Max[Axis]};
        for(const auto& Other:Boxes) for(double C:{Other.Min[Axis],Other.Max[Axis]}) if(C>B.Min[Axis] && C<B.Max[Axis]) Cuts.push_back(C);
        std::sort(Cuts.begin(),Cuts.end());Cuts.erase(std::unique(Cuts.begin(),Cuts.end()),Cuts.end());
        for(std::size_t N=1;N<Cuts.size();++N) {
            P[Axis]=(Cuts[N-1]+Cuts[N])*.5;
            bool Q[4];int Count=0;
            for(int I=0;I<4;++I) {Point Sample=P;Sample[J]+=(I&1 ? 1 : -1)*1e-6;Sample[K]+=(I&2 ? 1 : -1)*1e-6;Q[I]=Inside(Boxes,Sample);Count+=Q[I];}
            if(Count==0 || Count==4 || (Count==2 && Q[0]!=Q[3])) continue;
            Line L{P,P};L.A[Axis]=Cuts[N-1];L.B[Axis]=Cuts[N];
            bool Duplicate=false;for(const auto& Existing:Result) if(Existing.A==L.A && Existing.B==L.B) {Duplicate=true;break;}
            if(!Duplicate) Result.push_back(L);
        }
    }
    return Result;
}
}
