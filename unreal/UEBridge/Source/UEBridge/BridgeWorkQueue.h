#pragma once
#include <deque>
#include <unordered_set>

namespace BridgeWorkQueue {
// Pop does not restart a sparse set iterator at its first allocation. A block
// may enqueue itself again after being popped (redstone propagation).
template<class Value,class Hash> class UniqueQueue {
    std::deque<Value> Order;
    std::unordered_set<Value,Hash> Pending;
public:
    bool Add(const Value& Item) {
        if(!Pending.insert(Item).second) return false;
        Order.push_back(Item);return true;
    }
    Value Pop() {Value Item=Order.front();Order.pop_front();Pending.erase(Item);return Item;}
    bool IsEmpty() const {return Order.empty();}
    std::size_t Num() const {return Order.size();}
    void Empty() {Order.clear();Pending.clear();}
};
}
