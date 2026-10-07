#include "BridgeOutlineMath.h"
#include <iostream>
#include <stdexcept>
using namespace BridgeOutlineMath;
void Check(bool Condition,const char* Message) {if(!Condition) throw std::runtime_error(Message);}
int main() {
    Check(Edges({{{0,0,0},{1,1,1}}}).size()==12,"full cube keeps exactly twelve edges");
    const auto Door=Edges({{{0,0,0},{.1875,1,1}}});Check(Door.size()==12,"closed thin door has twelve edges");
    for(const auto& E:Door) Check(E.A[0]<=.1875 && E.B[0]<=.1875,"thin door does not outline the surrounding cube");
    const auto Open=Edges({{{0,0,0},{1,1,.1875}}});for(const auto& E:Open) Check(E.A[2]<=.1875 && E.B[2]<=.1875,"opening rotates thin outline");
    const auto Joined=Edges({{{0,0,0},{1,1,1}},{{1,0,0},{2,1,1}}});
    for(const auto& E:Joined) Check(!(E.A[0]==1 && E.B[0]==1),"joined shapes suppress internal face lines");
    const auto Stair=Edges({{{0,0,0},{1,.5,1}},{{.5,.5,0},{1,1,1}}});Check(Stair.size()>12,"stairs preserve concave boundary creases");
    Check(Edges({}).empty(),"empty native outline is empty");
    Check(Edges({{{0,0,0},{1,1,1}},{{0,0,0},{1,1,1}}}).size()==12,"duplicate boxes do not duplicate edges");
    std::cout << "outline math: 8 checks passed\n";
}
