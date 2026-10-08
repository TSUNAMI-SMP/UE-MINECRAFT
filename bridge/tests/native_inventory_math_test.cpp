#include "../../unreal/UEBridge/Source/UEBridge/BridgeNativeInventoryMath.h"
#include <cassert>
#include <numeric>

using BridgeNativeInventoryMath::distribute;
int main() {
    // ScreenHandler calculates floor(cursor/selected slot count), leaving the remainder on the cursor.
    auto even=distribute(10,0,{{0,64},{0,64},{0,64}});
    assert((even.counts==std::vector<int>{3,3,3}));assert(even.cursor==1);
    // A nearly full slot receives less, with the unapplied portion retained rather than reassigned.
    auto capped=distribute(12,0,{{63,64},{2,64},{0,64}});
    assert((capped.counts==std::vector<int>{64,6,4}));assert(capped.cursor==3);
    assert(std::accumulate(capped.counts.begin(),capped.counts.end(),capped.cursor)==12+63+2);
    auto one=distribute(20,1,{{4,64},{0,64},{64,64}});
    assert((one.counts==std::vector<int>{5,1,64}));assert(one.cursor==18);
    auto armor=distribute(3,0,{{0,1},{0,1}});
    assert((armor.counts==std::vector<int>{1,1}));assert(armor.cursor==1);
    // Creative middle drag fills admitted slots without survival quantity conservation.
    auto creative=distribute(1,2,{{0,64},{5,64},{0,1}});
    assert((creative.counts==std::vector<int>{64,64,1}));assert(creative.cursor==0);
    assert(distribute(1,0,{{0,64},{0,64}}).counts.empty());
    assert(distribute(0,1,{{0,64}}).counts.empty());
    assert(distribute(4,3,{{0,64}}).counts.empty());
    return 0;
}
