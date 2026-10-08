#pragma once
#include <algorithm>
#include <vector>

/** ScreenHandler.quickCraft / calculateStackSize translated from Minecraft 1.21.11.
 * Item compatibility and slot admission are checked by the inventory before this count-only calculation. */
namespace BridgeNativeInventoryMath {
struct Slot { int count; int capacity; };
struct Distribution { std::vector<int> counts; int cursor; };
inline Distribution distribute(int cursor,int mode,const std::vector<Slot>& slots) {
    Distribution out;out.cursor=cursor;
    if(cursor<=0 || slots.empty() || mode<0 || mode>2 || (mode!=2 && cursor<int(slots.size()))) return out;
    const int share=mode==0 ? cursor/int(slots.size()) : (mode==1 ? 1 : 0);
    for(const Slot& slot:slots) {
        const int finalCount=std::min(slot.capacity,slot.count+(mode==2 ? slot.capacity : share));
        out.counts.push_back(finalCount);out.cursor-=finalCount-slot.count;
    }
    out.cursor=std::max(0,out.cursor);return out;
}
}
