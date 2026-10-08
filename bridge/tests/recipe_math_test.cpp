#include "BridgeRecipeMath.h"
#include "BridgeCombatMath.h"
#include <cassert>
#include <iostream>
using namespace BridgeRecipeMath;
int main() {
    Recipe axe{"axe","minecraft:crafting_shaped","axe",2,3,1,200,{{"plank"},{"plank"},{"plank"},{"stick"},{},{"stick"}}};
    assert(Matches(axe,{"plank","plank","","plank","stick","","","stick",""},3,3));
    assert(Matches(axe,{"","plank","plank","","stick","plank","","stick",""},3,3));
    assert(!Matches(axe,{"plank","plank","","plank","stick","","extra","stick",""},3,3));
    assert(!Matches(axe,{"plank","plank","plank","stick"},2,2));
    Recipe log{"planks","minecraft:crafting_shapeless","plank",0,0,4,200,{{"oak","birch"}}};
    for(int y=0;y<3;++y) for(int x=0;x<3;++x) {std::vector<std::string> grid(9);grid[x+y*3]="birch";assert(Matches(log,grid,3,3));grid[(x+y*3+1)%9]="oak";assert(!Matches(log,grid,3,3));}
    // Greedy chooses oak for the tag and incorrectly rejects the exact oak slot.
    Recipe overlapping{"overlap","minecraft:crafting_shapeless","result",0,0,1,200,{{"oak","birch"},{"oak"}}};
    assert(Matches(overlapping,{"oak","birch","",""},2,2));
    assert(Matches(overlapping,{"birch","oak","",""},2,2));
    assert(!Matches(overlapping,{"birch","birch","",""},2,2));
    Recipe empty=overlapping;empty.ingredients.clear();assert(!Matches(empty,{"","","",""},2,2));
    Recipe cooking{"iron","minecraft:smelting","ingot",0,0,1,200,{{"ore","raw_iron"}}};
    assert(SingleMatches(cooking,"raw_iron","minecraft:smelting"));assert(!SingleMatches(cooking,"raw_iron","minecraft:smoking"));assert(!SingleMatches(cooking,"","minecraft:smelting"));
    // LivingEntity's settled Y is (0 - .08)*.98, then takeKnockback halves it.
    const auto ordinary=BridgeCombatMath::knockbackVelocity({0,0,BridgeCombatMath::groundedVerticalVelocity(0)},.4,0,1,0,true);
    assert(std::abs(ordinary.z-721.6)<1.e-9);
    const auto sprint=BridgeCombatMath::knockbackVelocity(ordinary,.5,0,1,0,true);
    assert(std::abs(sprint.z-800)<1.e-9);
    assert(std::abs(BridgeCombatMath::groundedVerticalVelocity(123)-123)<1.e-9);
    std::cout<<"Recipe translation, mirroring, overlapping tags, extra ingredients, cooking, and grounded attack fixtures passed\n";
}
