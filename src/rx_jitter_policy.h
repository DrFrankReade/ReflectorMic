#pragma once
#include <cstdint>

// Network-task-only playout clock. Adapt from actual starvation rather than
// individual arrival gaps: the voice/data filter releases groups of 2–3 frames.
// No codec calls, floating point, allocation, or waits on the network task.
class RxJitterPolicy {
 public:
  static constexpr uint8_t MIN_FRAMES = 4;
  static constexpr uint8_t MAX_FRAMES = 12;
  static constexpr uint32_t FRAME_MS = 20;

  void reset() { *this = RxJitterPolicy(); }
  uint8_t targetFrames() const { return target_; }
  uint32_t refills() const { return refills_; }

  bool ready(uint32_t now, uint8_t available, bool ending) {
    if (!initialized_) {
      initialized_ = true;
      waitSince_ = stableSince_ = now;
    }
    if (waiting_) {
      // End markers bypass prefill so even a one-frame call can drain.
      if (!ending && (available == 0 ||
          (available < target_ && now - waitSince_ < target_ * FRAME_MS)))
        return false;
      waiting_ = false;
      next_ = now;
    }
    if (static_cast<int32_t>(now - next_) < 0) return false;
    if (!available && !ending) {
      target_ = target_ + 2 > MAX_FRAMES ? MAX_FRAMES : target_ + 2;
      waiting_ = true;
      waitSince_ = stableSince_ = now;
      ++refills_;
      return false;
    }
    return true;
  }

  void played(uint32_t now) {
    // Never deliver a burst of overdue frames into the decoder after a stall.
    next_ = now - next_ >= FRAME_MS ? now + FRAME_MS : next_ + FRAME_MS;
  }

  bool relax(uint32_t now) {
    if (!initialized_ || waiting_ || now - stableSince_ < 10000) return false;
    stableSince_ = now;
    if (target_ <= MIN_FRAMES) return false;
    --target_;
    return true;
  }

 private:
  uint8_t target_ = 6;  // Start at 120 ms, adapt within 80–240 ms.
  bool initialized_ = false, waiting_ = true;
  uint32_t waitSince_ = 0, next_ = 0, stableSince_ = 0, refills_ = 0;
};
