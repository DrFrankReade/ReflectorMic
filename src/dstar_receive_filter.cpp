#include "dstar_receive_filter.h"
#include <cstring>

namespace {
constexpr uint8_t SYNC[3] = {0x55, 0x2d, 0x16};
constexpr uint32_t MAX_REORDER_MS = 240;
} // namespace

void DStarReceiveFilter::reset() {
  for (auto &slot : slots_)
    slot.index = -1;
  highest_ = next_ = -1;
  waitingSince_ = 0;
  syncSequence_ = -1;
}

uint8_t DStarReceiveFilter::phase(int32_t index) const {
  return (index + 21 - syncSequence_) % 21;
}

int32_t DStarReceiveFilter::blockStart(int32_t index) const {
  const int32_t seq = phase(index);
  return index - (seq <= 2 ? seq : (seq % 2 == 0 ? 1 : 0));
}

void DStarReceiveFilter::alignToSync(int32_t index, uint32_t now) {
  // Once anchored, a payload which happens to contain sync bytes must not
  // move the block boundaries. A new stream/header reset acquires afresh.
  if (syncSequence_ >= 0)
    return;
  syncSequence_ = index % 21;
  int32_t first = index;
  for (auto &slot : slots_) {
    if (slot.index >= highest_ - 20 && slot.index >= 0) {
      if (slot.index < first)
        first = slot.index;
    } else {
      slot.index = -1;
    }
  }
  // Replay the bounded pre-sync window using the observed phase. This keeps
  // a late first sync from unnecessarily discarding its preceding voice pairs.
  next_ = blockStart(first);
  waitingSince_ = now;
}

DStarReceiveFilter::Kind DStarReceiveFilter::classify(uint8_t h, bool sync) {
  // Slow data is discarded, but the accompanying voice is retained. Reserved
  // or damaged markers fail closed for this block only, never for the call.
  if ((h >= 0x31 && h <= 0x35) || (h >= 0x40 && h <= 0x43) ||
      (h >= 0x51 && h <= 0x55) || h == 0x66 || h == 0xc2)
    return Kind::Voice;
  if (h >= 0x81 && h <= (sync ? 0x9c : 0x94))
    return Kind::FastData;
  return Kind::Unknown;
}

bool DStarReceiveFilter::push(uint8_t sequence, const uint8_t frame[12],
                              uint32_t now) {
  if (sequence > 20 || !frame)
    return false;
  // Start one cycle above zero so a leading partial block can start before
  // the first received packet without negative slot indices.
  int32_t index = sequence + 21;
  if (highest_ >= 0) {
    const int32_t forward = (sequence + 21 - highest_ % 21) % 21;
    index = highest_ + (forward <= 10 ? forward : forward - 21);
    if (index < 0 || (syncSequence_ >= 0 && index < next_))
      return false;
  }
  if (index > highest_)
    highest_ = index;
  if (!memcmp(frame + 9, SYNC, sizeof(SYNC)))
    alignToSync(index, now);
  // No mini-header is trusted until a real sync establishes the phase. Keep
  // at most one cycle for late entry; an unanchored stream cannot become audio.
  // Allow two missing leading slots in the first three-frame block.
  if (syncSequence_ >= 0 && highest_ - next_ >= 23) {
    next_ = blockStart(highest_);
    waitingSince_ = now;
  }
  auto &slot = slots_[index % 21];
  if (slot.index >= index)
    return false;
  const uint8_t count = syncSequence_ >= 0 && phase(next_) == 0 ? 3 : 2;
  if (syncSequence_ >= 0 && index < next_ + count) {
    bool any = false;
    for (uint8_t i = 0; i < count; ++i)
      any |= slots_[(next_ + i) % 21].index == next_ + i;
    if (!any)
      waitingSince_ = now;
  }
  slot.index = index;
  memcpy(slot.frame, frame, 12);
  return true;
}

bool DStarReceiveFilter::pop(uint32_t now, Block &out, bool ending, uint32_t reorderWaitMs) {
  if (syncSequence_ < 0 || next_ < 0 || next_ > highest_)
    return false;
  const uint8_t sequence = phase(next_);
  const uint8_t count = sequence == 0 ? 3 : 2;
  uint8_t present = 0;
  for (uint8_t i = 0; i < count; ++i)
    if (slots_[(next_ + i) % 21].index == next_ + i)
      present |= 1U << i;
  // The active voice stream borrows its adaptive playout budget to allow
  // reordering before classification irreversibly discards an incomplete block.
  const uint32_t waitMs = reorderWaitMs > MAX_REORDER_MS ? MAX_REORDER_MS : reorderWaitMs;
  const int32_t markerIndex = next_ + (sequence == 0 ? 1 : 0);
  const auto &marker = slots_[markerIndex % 21];
  const bool haveMarker = marker.index == markerIndex;
  // A stalled partial block has no later audio to hold up. Give its missing
  // packets up to 240 ms to arrive, even at a smaller adaptive target.
  // Expiring it at the normal deadline used to discard valid voice when the
  // header arrived just afterward. Later blocks, end-of-stream and the hard
  // limit still release the wait; no inferred voice or unbounded backlog.
  if (present != (1U << count) - 1 && !ending && highest_ < next_ + count &&
      now - waitingSince_ < MAX_REORDER_MS)
    return false;
  const int32_t lookahead = waitMs / 20 > 3 ? waitMs / 20 : 3;
  if (present != (1U << count) - 1 && !ending && highest_ < next_ + count + lookahead &&
      now - waitingSince_ < waitMs)
    return false;
  out = Block{};
  out.sequence = next_ % 21; // Preserve the transport sequence for jitter pacing.
  out.count = count;
  out.present = present;
  if (haveMarker) {
    out.kind = classify(marker.frame[9] ^ 0x70, sequence == 0);
    if (out.kind == Kind::Unknown)
      out.reason = Reason::InvalidMarker;
  } else {
    out.reason = Reason::MissingMarker;
  }
  if (sequence == 0 && (present & 1) &&
      memcmp(slots_[next_ % 21].frame + 9, SYNC, 3)) {
    out.kind = Kind::Unknown;
    out.reason = Reason::SyncMismatch;
  }
  for (uint8_t i = 0; i < count; ++i) {
    auto &slot = slots_[(next_ + i) % 21];
    if (present & (1U << i)) {
      memcpy(out.ambe[i], slot.frame, 9);
      slot.index = -1;
    }
  }
  next_ += count;
  waitingSince_ = now;
  return true;
}
