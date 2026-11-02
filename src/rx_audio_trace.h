#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

// A bounded diagnostic tap, never part of codec prediction or packet timing.
// Only the audio worker writes storage. HTTP requests a snapshot and reads it
// after the release/acquire publication; the HTTP server serializes downloads
// and subsequent requests. Storage is allocated once in PSRAM by AudioEngine.
struct RxTraceFrame {
  uint32_t timestampMs, stream, frame, decodeUs, flags;
  int32_t correctedErrors;
  uint16_t peak;
  uint8_t decoded;
  uint8_t ambe[9];
  int16_t pcm[160];
};
static_assert(sizeof(RxTraceFrame) == 356, "RX trace wire layout");

struct RxTraceSnapshot {
  char magic[8];
  uint32_t version, count, recordBytes, sampleRate, recoveries, reserved;
  RxTraceFrame frames[128];
};
static_assert(sizeof(RxTraceSnapshot) == 32 + 128 * 356, "RX trace header layout");

class RxAudioTrace {
 public:
  void begin(RxTraceFrame* ring, RxTraceSnapshot* snapshot) {
    ring_ = ring;
    snapshot_ = snapshot;
  }
  bool request() {
    if (!ring_ || !snapshot_) return false;
    if (state_.load() == 1) return false;
    state_.store(1);
    return true;
  }
  const RxTraceSnapshot* snapshot() const {
    return state_.load() == 2 ? snapshot_ : nullptr;
  }
  void push(const RxTraceFrame& frame) {
    if (!ring_) return;
    ring_[next_] = frame;
    next_ = (next_ + 1) % 128;
    if (count_ < 128) ++count_;
  }
  void service(uint32_t recoveries) {
    if (state_.load() != 1) return;
    memcpy(snapshot_->magic, "RMRX0001", 8);
    snapshot_->version = 1;
    snapshot_->count = count_;
    snapshot_->recordBytes = sizeof(RxTraceFrame);
    snapshot_->sampleRate = 8000;
    snapshot_->recoveries = recoveries;
    snapshot_->reserved = 0;
    for (uint32_t i = 0; i < count_; ++i)
      snapshot_->frames[i] = ring_[(next_ + 128 - count_ + i) % 128];
    state_.store(2);
  }
 private:
  RxTraceFrame* ring_ = nullptr;
  RxTraceSnapshot* snapshot_ = nullptr;
  uint32_t next_ = 0, count_ = 0;
  std::atomic<uint8_t> state_{0};
};
