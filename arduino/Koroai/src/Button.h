#pragma once
#include <cstdint>
namespace koroai {
// Active-low button: require release after boot and a stable 50 ms edge.
class Button {
 public:
  bool pressed(bool rawPressed, uint32_t nowMs) {
    if (rawPressed != raw_) { raw_ = rawPressed; changedAt_ = nowMs; }
    if (static_cast<uint32_t>(nowMs - changedAt_) < 50 || stable_ == raw_) return false;
    stable_ = raw_;
    return stable_;
  }
 private:
  bool raw_ = false;
  bool stable_ = true;
  uint32_t changedAt_ = 0;
};
}  // namespace koroai
