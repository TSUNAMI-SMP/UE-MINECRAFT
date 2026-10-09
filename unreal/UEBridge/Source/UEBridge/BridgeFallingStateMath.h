#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace BridgeFallingState {
constexpr int ExpiredAge=601;
inline int AdvanceAge(int age) {return age>=ExpiredAge?ExpiredAge:age+1;}
inline bool RestoreAge(double value,int& age) {
    if(!std::isfinite(value)||value<0||value>std::numeric_limits<std::int32_t>::max()||std::floor(value)!=value) return false;
    age=value>=ExpiredAge?ExpiredAge:static_cast<int>(value);return true;
}
// Legacy transition waits could retain expired, stationary entities far below
// the finite world. Recover the quantity into its loaded X/Z column, not void.
inline bool RecoverBelowBoundary(double y,double bottom,int age,bool stationary) {
    return std::isfinite(y)&&std::isfinite(bottom)&&y<bottom&&age==ExpiredAge&&stationary;
}
}
