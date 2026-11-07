#pragma once
#include <cstdint>

// JARL chapters 6/7: sync + two frames form a block, then nine pairs.
// Gateways can offset packet sequence numbers from that on-air cycle.
// The mini-header follows the actual slow-data sync, NOT AMBE. Holding
// a whole block also prevents its sync frame escaping before classification.
// Fixed storage, no allocation or codec work; owned by the network task.
class DStarReceiveFilter {
public:
  enum class Kind : uint8_t { Voice, FastData, Unknown };
  enum class Reason : uint8_t { None, MissingMarker, InvalidMarker, SyncMismatch };
  struct Block {
    Kind kind = Kind::Unknown;
    Reason reason = Reason::None;
    uint8_t sequence = 0, count = 0, present = 0;
    uint8_t ambe[3][9]{};
  };
  void reset();
  bool push(uint8_t sequence, const uint8_t frame[12], uint32_t now);
  bool pop(uint32_t now, Block &out, bool ending = false,
           uint32_t reorderWaitMs = 60);

private:
  struct Slot {
    int32_t index = -1;
    uint8_t frame[12]{};
  };
  uint8_t phase(int32_t index) const;
  int32_t blockStart(int32_t index) const;
  void alignToSync(int32_t index, uint32_t now);
  static Kind classify(uint8_t header, bool syncBlock);
  Slot slots_[21];
  int32_t highest_ = -1, next_ = -1;
  uint32_t waitingSince_ = 0;
  int8_t syncSequence_ = -1;
};
