#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// One microphone producer and one codec consumer. Storage lives in PSRAM;
// only the atomic cursors live in internal RAM. Publishing a cursor transfers
// ownership of a complete frame, so the encoder never sees a partial write.
struct CapturedPcm {
  uint32_t completedMs;
  int16_t samples[160];
};

class PcmCaptureBuffer {
 public:
  // Call only with both tasks idle. Allocate capacity + 1 storage elements;
  // the unused slot distinguishes full from empty without a shared counter.
  void reset(CapturedPcm *storage, uint32_t capacity) {
    storage_ = storage;
    slots_ = capacity + 1;
    read_.store(0);
    write_.store(0);
  }
  bool push(const CapturedPcm &frame) {
    const uint32_t index = write_.load(std::memory_order_relaxed);
    const uint32_t next = (index + 1) % slots_;
    if (next == read_.load(std::memory_order_acquire)) return false;
    storage_[index] = frame;
    write_.store(next, std::memory_order_release);
    return true;
  }
  bool pop(CapturedPcm &frame) {
    const uint32_t index = read_.load(std::memory_order_relaxed);
    if (index == write_.load(std::memory_order_acquire)) return false;
    frame = storage_[index];
    read_.store((index + 1) % slots_, std::memory_order_release);
    return true;
  }
  uint32_t pending() const {
    const uint32_t read = read_.load(std::memory_order_acquire);
    const uint32_t write = write_.load(std::memory_order_acquire);
    return (write + slots_ - read) % slots_;
  }

 private:
  CapturedPcm *storage_ = nullptr;
  uint32_t slots_ = 1;
  std::atomic<uint32_t> read_{0};
  std::atomic<uint32_t> write_{0};
};
