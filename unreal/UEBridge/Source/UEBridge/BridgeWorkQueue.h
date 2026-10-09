#pragma once
#include <deque>
#include <unordered_set>

namespace BridgeWorkQueue {
// A budget spans every catch-up tick in one rendered frame. Work already
// started may finish beyond the deadline; no pending task is discarded.
class FrameBudget {
    double Remaining;
    std::size_t Limit, Work=0;
public:
    FrameBudget(double Seconds,std::size_t MaxWork):Remaining(Seconds),Limit(MaxWork) {}
    bool Available() const {return Remaining>0 && Work<Limit;}
    double SecondsLeft() const {return Remaining;}
    void Charge(double Seconds) {Remaining-=Seconds;++Work;}
    void ChargeTime(double Seconds) {Remaining-=Seconds;}
};
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
