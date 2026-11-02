#include "dstar_header.h"
#include "reflector_client.h"
#include <cstring>

namespace {
constexpr uint32_t STREAM_TIMEOUT_MS = 1200;
constexpr uint8_t STARTUP_JITTER_FRAMES = 4;
constexpr uint8_t NULL_AMBE[9] = {0x9e, 0x8d, 0x32, 0x88, 0x26,
                                  0x1a, 0x3f, 0x61, 0xe8};
} // namespace

ReflectorClient::ReceiveCandidate *
ReflectorClient::candidateFor(uint16_t stream) {
  for (auto &candidate : candidates_)
    if (candidate.used && candidate.stream == stream)
      return &candidate;
  return nullptr;
}

void ReflectorClient::clearReceiveCandidates() {
  for (auto &candidate : candidates_)
    candidate.used = false;
}

void ReflectorClient::rememberIncomingHeader(const uint8_t *header,
                                             uint16_t stream) {
  if (candidateFor(stream))
    return; // Redundancy must not reset prediction.
  ReceiveCandidate *selected = nullptr;
  const uint32_t now = millis();
  for (auto &candidate : candidates_) {
    if (!candidate.used) {
      selected = &candidate;
      break;
    }
    if (rxActive_ && candidate.stream == rxStreamId_)
      continue;
    if (!selected || now - candidate.lastMediaMs > now - selected->lastMediaMs)
      selected = &candidate;
  }
  if (!selected)
    return;
  selected->used = true;
  selected->stream = stream;
  selected->lastMediaMs = now;
  memcpy(selected->header, header, sizeof(selected->header));
  selected->filter.reset();
}

void ReflectorClient::acceptFrame(const uint8_t *frame, uint16_t streamId,
                                  uint8_t sequence, bool end) {
  if (!linked() || txActive_) {
    ++busyMediaDrops_;
    return;
  }
  auto *candidate = candidateFor(streamId);
  if (!candidate) {
    ++voiceWithoutHeader_;
    return;
  }
  if (rxActive_ && streamId != rxStreamId_)
    ++wrongStreamFrames_;
  if (end) {
    ++receivedEndPackets_;
    drainCandidate(*candidate, true);
    candidate->used = false;
    if (rxActive_ && streamId == rxStreamId_) {
      rxEndPending_ = true;
      if (!nextPlayoutMs_)
        nextPlayoutMs_ = millis();
    }
    return;
  }
  if (!candidate->filter.push(sequence, frame, millis())) {
    ++droppedRxFrames_;
    return;
  }
  candidate->lastMediaMs = millis();
  drainCandidate(*candidate);
}

void ReflectorClient::serviceIncomingStreams() {
  const uint32_t now = millis();
  // Competing packets and repeated headers cannot keep a missing owner alive.
  if (rxActive_ && now - rxLastMediaMs_ > STREAM_TIMEOUT_MS)
    finishIncoming();
  for (auto &candidate : candidates_) {
    if (!candidate.used)
      continue;
    if (now - candidate.lastMediaMs > STREAM_TIMEOUT_MS) {
      candidate.used = false;
      continue;
    }
    drainCandidate(candidate);
  }
}

void ReflectorClient::drainCandidate(ReceiveCandidate &candidate, bool ending) {
  DStarReceiveFilter::Block block;
  while (candidate.filter.pop(millis(), block, ending))
    acceptBlock(candidate, block);
}

void ReflectorClient::acceptBlock(const ReceiveCandidate &candidate,
                                  const DStarReceiveFilter::Block &block) {
  using Kind = DStarReceiveFilter::Kind;
  if (block.kind == Kind::FastData) {
    fastDataFrames_ += block.count;
    if (rxActive_ && candidate.stream == rxStreamId_) {
      // Drain the already accepted speech tail, then release audio ownership.
      // A long image transfer must not reserve the speaker against voice.
      rxEndPending_ = true;
      if (!nextPlayoutMs_)
        nextPlayoutMs_ = millis();
    }
    return; // Future data destinations belong here, outside the audio path.
  }
  if (block.kind == Kind::Unknown)
    unclassifiedFrames_ += block.count;
  if (rxActive_ && candidate.stream != rxStreamId_) {
    if (block.kind == Kind::Voice)
      competingVoiceFrames_ += block.count;
    return; // First VOICE stream wins, not first header/data stream.
  }
  if (!rxActive_) {
    if (block.kind != Kind::Voice || !block.present)
      return;
    ++acceptedHeaders_;
    beginIncoming(candidate.header, candidate.stream);
  }
  if (rxEndPending_)
    return;
  // Corrupt/unknown blocks must not reserve the speaker forever. The candidate
  // has its own raw-packet timeout; ownership is refreshed only by known voice.
  if (block.kind == Kind::Voice)
    rxLastMediaMs_ = millis();
  for (uint8_t i = 0; i < block.count; ++i) {
    const bool present = (block.present & (1U << i)) != 0;
    const bool speech = present && block.kind == Kind::Voice;
    if (!present)
      continue; // Existing jitter handling conceals packet loss.
    const uint8_t *ambe = speech ? block.ambe[i] : NULL_AMBE;
    if (speech) {
      ++acceptedVoiceFrames_;
      if (!memcmp(ambe, NULL_AMBE, 9))
        ++receivedNullFrames_;
    }
    queueIncomingFrame(ambe, (block.sequence + i) % 21);
  }
}

void ReflectorClient::beginIncoming(const uint8_t *header, uint16_t streamId) {
  if (rxActive_)
    finishIncoming();
  clearJitter();
  rxActive_ = true;
  rxStreamId_ = streamId;
  rxLastMediaMs_ = millis();
  IncomingCallInfo info;
  info.rpt2 = dstar::readField(header + 3, 8);
  info.rpt1 = dstar::readField(header + 11, 8);
  info.destination = dstar::readField(header + 19, 8);
  info.from = dstar::readField(header + 27, 8);
  info.suffix = dstar::readField(header + 35, 4);
  info.streamId = streamId;
  lastHeard_ = info.from;
  if (rxStart_)
    rxStart_(info, callbackContext_);
}

void ReflectorClient::queueIncomingFrame(const uint8_t *ambe,
                                         uint8_t sequence) {
  int32_t index = 0;
  if (rxLastSequence_ < 0) {
    rxLastSequence_ = sequence;
    rxHighestIndex_ = 0;
  } else {
    const uint8_t forward = dplus::sequenceDistance(rxLastSequence_, sequence);
    if (forward == 0) {
      droppedRxFrames_++;
      return;
    }
    if (forward <= 10) {
      index = rxHighestIndex_ + forward;
      rxHighestIndex_ = index;
      rxLastSequence_ = sequence;
    } else {
      index = rxHighestIndex_ - (21 - forward);
      if (index < rxPlayIndex_) {
        droppedRxFrames_++;
        return;
      }
    }
  }
  if (rxHighestIndex_ == 0 && rxLastSequence_ == sequence)
    index = 0;
  if (index < rxPlayIndex_) {
    droppedRxFrames_++;
    return;
  }
  trimJitterWindow(index);
  JitterSlot &slot = jitter_[index % JITTER_SLOTS];
  if (slot.valid) {
    droppedRxFrames_++;
    if (slot.index == index)
      return; // Late duplicate must not inflate occupancy.
  } else {
    bufferedFrames_++;
  }
  slot.valid = true;
  slot.index = index;
  memcpy(slot.ambe, ambe, 9);
  if (nextPlayoutMs_ == 0 && bufferedFrames_ >= STARTUP_JITTER_FRAMES) {
    nextPlayoutMs_ = millis() + 20;
  }
}

void ReflectorClient::trimJitterWindow(int32_t newestIndex) {
  if (newestIndex - rxPlayIndex_ < JITTER_SLOTS)
    return;
  // A burst can lap this ring, particularly when a stream starts while HTTP
  // or a flash write holds the main loop. Overwriting slots without moving
  // the read cursor traps playout behind the writer: every subsequent frame
  // is replaced by silence even though packets continue arriving normally.
  // Drop old latency once, keep a small recent window and resume at 20 ms.
  const int32_t resume = newestIndex - STARTUP_JITTER_FRAMES + 1;
  droppedRxFrames_ += static_cast<uint32_t>(resume - rxPlayIndex_);
  rxPlayIndex_ = resume;
  bufferedFrames_ = 0;
  for (auto &slot : jitter_) {
    if (slot.valid && slot.index < resume)
      slot.valid = false;
    if (slot.valid)
      ++bufferedFrames_;
  }
  nextPlayoutMs_ = millis() + 20;
  ++jitterResyncs_;
}

void ReflectorClient::serviceJitterBuffer() {
  if (!rxActive_ || nextPlayoutMs_ == 0)
    return;
  uint8_t serviced = 0;
  uint32_t now = millis();
  while (static_cast<int32_t>(now - nextPlayoutMs_) >= 0 && serviced++ < 4) {
    JitterSlot &slot = jitter_[rxPlayIndex_ % JITTER_SLOTS];
    if (slot.valid && slot.index == rxPlayIndex_) {
      if (rxFrame_)
        rxFrame_(slot.ambe, callbackContext_);
      slot.valid = false;
      if (bufferedFrames_)
        bufferedFrames_--;
    } else if (rxPlayIndex_ <= rxHighestIndex_ || rxEndPending_) {
      if (rxFrame_)
        rxFrame_(NULL_AMBE, callbackContext_);
      droppedRxFrames_++;
      ++concealedRxFrames_;
    } else {
      nextPlayoutMs_ = now + 20;
      break;
    }
    rxPlayIndex_++;
    nextPlayoutMs_ += 20;
    if (rxEndPending_ && rxPlayIndex_ > rxHighestIndex_) {
      finishIncoming();
      break;
    }
  }
}

void ReflectorClient::finishIncoming() {
  if (!rxActive_)
    return;
  rxActive_ = false;
  clearJitter();
  if (rxEnd_)
    rxEnd_(callbackContext_);
}

void ReflectorClient::clearJitter() {
  for (JitterSlot &slot : jitter_)
    slot = JitterSlot();
  rxEndPending_ = false;
  rxStreamId_ = 0;
  rxLastSequence_ = -1;
  rxHighestIndex_ = -1;
  rxPlayIndex_ = 0;
  nextPlayoutMs_ = 0;
  bufferedFrames_ = 0;
}
