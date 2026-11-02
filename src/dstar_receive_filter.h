#pragma once
#include <cstdint>

// JARL chapters 6/7: seq 0..2 form one block, then 3..4, 5..6, ...19..20.
// The mini-header is in the odd frame's slow-data byte, NOT in AMBE. Holding
// a whole block also prevents its sync frame escaping before classification.
// Fixed storage, no allocation or codec work; owned by the network task.
class DStarReceiveFilter {
public:
  enum class Kind : uint8_t { Voice, FastData, Unknown };
  struct Block {
    Kind kind = Kind::Unknown;
    uint8_t sequence = 0, count = 0, present = 0;
    uint8_t ambe[3][9]{};
  };
  void reset();
  bool push(uint8_t sequence, const uint8_t frame[12], uint32_t now);
  bool pop(uint32_t now, Block &out, bool ending = false);

private:
  struct Slot {
    int32_t index = -1;
    uint8_t frame[12]{};
  };
  static int32_t blockStart(int32_t index);
  static Kind classify(uint8_t header, bool syncBlock);
  Slot slots_[21];
  int32_t highest_ = -1, next_ = -1;
  uint32_t waitingSince_ = 0;
};
