#include "dstar_receive_filter.h"
#include <cstring>

namespace {
constexpr uint32_t REORDER_WAIT_MS = 60;
constexpr uint8_t SYNC[3] = {0x55, 0x2d, 0x16};
} // namespace

void DStarReceiveFilter::reset() {
  for (auto &slot : slots_)
    slot.index = -1;
  highest_ = next_ = -1;
  waitingSince_ = 0;
}

int32_t DStarReceiveFilter::blockStart(int32_t index) {
  const int32_t seq = index % 21;
  return index - (seq <= 2 ? seq : (seq % 2 == 0 ? 1 : 0));
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
  int32_t index = sequence;
  if (highest_ >= 0) {
    const int32_t forward = (sequence + 21 - highest_ % 21) % 21;
    index = highest_ + (forward <= 10 ? forward : forward - 21);
    if (index < next_)
      return false;
  } else {
    next_ = blockStart(index);
    waitingSince_ = now;
  }
  if (index > highest_)
    highest_ = index;
  // Normally pop() is called after each push. Bound storage even if the caller
  // falls behind: abandon stale blocks rather than alias a later cycle.
  if (highest_ - next_ >= 21) {
    next_ = blockStart(highest_);
    waitingSince_ = now;
  }
  auto &slot = slots_[index % 21];
  if (slot.index == index)
    return false;
  const uint8_t count = next_ % 21 == 0 ? 3 : 2;
  if (index < next_ + count) {
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

bool DStarReceiveFilter::pop(uint32_t now, Block &out, bool ending) {
  if (next_ < 0 || next_ > highest_)
    return false;
  const uint8_t sequence = next_ % 21;
  const uint8_t count = sequence == 0 ? 3 : 2;
  uint8_t present = 0;
  for (uint8_t i = 0; i < count; ++i)
    if (slots_[(next_ + i) % 21].index == next_ + i)
      present |= 1U << i;
  if (present != (1U << count) - 1 && !ending && highest_ < next_ + count + 3 &&
      now - waitingSince_ < REORDER_WAIT_MS)
    return false;
  out = Block{};
  out.sequence = sequence;
  out.count = count;
  out.present = present;
  const auto &marker = slots_[(next_ + (sequence == 0 ? 1 : 0)) % 21];
  if (marker.index == next_ + (sequence == 0 ? 1 : 0))
    out.kind = classify(marker.frame[9] ^ 0x70, sequence == 0);
  if (sequence == 0 && (present & 1) &&
      memcmp(slots_[next_ % 21].frame + 9, SYNC, 3))
    out.kind = Kind::Unknown;
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
