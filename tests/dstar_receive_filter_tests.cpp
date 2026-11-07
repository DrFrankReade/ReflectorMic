#include "dstar_receive_filter.h"
#include <cassert>
#include <cstring>
#include <iostream>

using Filter = DStarReceiveFilter;
void push(Filter &f, unsigned seq, uint8_t type, uint32_t now) {
  // Real capture fragment: its middle byte is 0x66. Mini-header alone decides
  // whether those exact bytes are data or voice, so no AMBE byte heuristic.
  uint8_t frame[12] = {
      0x54, 0x1f, 0xfa, 0x23, 0x66,
      0x58, 0x26, 0x7d, 0x1d, static_cast<uint8_t>(type ^ 0x70),
      0x29, 0xf5};
  if (seq == 0) {
    frame[9] = 0x55;
    frame[10] = 0x2d;
    frame[11] = 0x16;
  }
  assert(f.push(seq, frame, now));
}
void prime(Filter &f) {
  f.reset();
  push(f, 0, 0x66, 0); push(f, 1, 0x66, 0); push(f, 2, 0x66, 0);
  Filter::Block block;
  assert(f.pop(0, block) && block.kind == Filter::Kind::Voice);
}
// Synthetic payloads reproduce the observed offset without retaining any
// station identity, text, location, or recorded speech.
void offsetCycles(unsigned offset, bool data, bool reorder, uint32_t now) {
  Filter f;
  f.reset();
  Filter::Block b;
  unsigned received = 0;
  auto drain = [&](bool ending) {
    while (f.pop(now, b, ending, 240)) {
      const unsigned initialPhase = (21 - offset) % 21;
      const bool missingInitialMarker = !received && initialPhase && initialPhase % 2 == 0;
      assert(b.kind == (missingInitialMarker ? Filter::Kind::Unknown :
                        data ? Filter::Kind::FastData : Filter::Kind::Voice));
      for (unsigned i = 0; i < b.count; ++i) {
        if (!(b.present & (1U << i))) continue;
        unsigned id = b.ambe[i][0] | (unsigned(b.ambe[i][1]) << 8);
        assert(id == received++);
        assert((b.sequence + i) % 21 == id % 21);
        assert(b.ambe[i][4] == 0x66); // Never inspect voice payload as a marker.
      }
    }
  };
  auto send = [&](unsigned id) {
    unsigned phase = (id + 21 - offset) % 21;
    uint8_t frame[12] = {uint8_t(id), uint8_t(id >> 8), 2, 3, 0x66, 5, 6, 7, 8,
                         0x16, 0x29, 0xf5};
    if (!phase) {
      frame[9] = 0x55; frame[10] = 0x2d; frame[11] = 0x16;
    } else if (phase % 2) {
      // Alternating header/text/DPRS types. Fast data uses every legal length.
      const uint8_t voice[] = {0x55, 0x40, 0x35, 0x66, 0xc2};
      frame[9] = (data ? 0x81 + (id / 21) % (phase == 1 ? 28 : 20)
                        : voice[(id / 21) % 5]) ^ 0x70;
    } else {
      // Continuation bytes deliberately look like VOICE mini-headers. A
      // parity mistake would admit fast data, just as it rejected real voice.
      frame[9] = 0x55 ^ 0x70;
    }
    assert(f.push(id % 21, frame, now));
    drain(false);
    now += 20;
  };
  // Finish at a complete block boundary; initial partial blocks with a
  // missing marker remain unknown rather than trusting their continuation.
  const unsigned total = 21 * 30 + offset;
  for (unsigned n = 0; n < total; ++n) {
    if (reorder && n % 21 == 3 && n + 1 < total) { send(n + 1); send(n); ++n; }
    else send(n);
  }
  drain(true);
  assert(received == total);
}

void offsetMissingMarker(unsigned offset) {
  Filter f; Filter::Block b;
  f.reset();
  uint8_t sync[12] = {1,2,3,4,5,6,7,8,9,0x55,0x2d,0x16};
  uint8_t continuation[12] = {1,2,3,4,5,6,7,8,9,0x16,0x29,0xf5};
  assert(f.push(offset, sync, 0));
  assert(f.push((offset + 2) % 21, continuation, 20));
  assert(!f.pop(239, b));
  assert(f.pop(240, b) && b.kind == Filter::Kind::Unknown && b.present == 5);
  assert(b.reason == Filter::Reason::MissingMarker);
  // A missing sync after acquisition keeps the known phase, but losing its
  // mini-header must still prevent picture payloads from entering audio.
  assert(f.push((offset + 4) % 21, continuation, 260));
  assert(!f.pop(499, b));
  assert(f.pop(500, b) && b.kind == Filter::Kind::Unknown && b.present == 2);
}

void unanchoredAndFalseSync() {
  Filter f; Filter::Block b;
  f.reset();
  // Even plausible headers cannot authorize audio before synchronization.
  for (unsigned i = 0; i < 100; ++i) {
    uint8_t frame[12] = {1,2,3,4,5,6,7,8,9,0x16,0x29,0xf5};
    assert(f.push(i % 21, frame, i * 20));
    assert(!f.pop(i * 20, b));
  }
  assert(!f.pop(3000, b, true));
  // Once anchored, sync-like bytes in a fast-data continuation cannot realign
  // the next marker onto payload bytes which happen to look like voice.
  prime(f);
  push(f, 3, 0x94, 20);
  uint8_t falseSync[12] = {1,2,3,4,5,6,7,8,9,0x55,0x2d,0x16};
  assert(f.push(4, falseSync, 40));
  assert(f.pop(40, b) && b.kind == Filter::Kind::FastData);
  push(f, 5, 0x94, 60); push(f, 6, 0x66, 80);
  assert(f.pop(80, b) && b.kind == Filter::Kind::FastData);
}

// A real timing pattern: sync arrives, followed by a 125/140 ms gap before
// the mini-header and continuation burst. No recorded speech is needed to
// reproduce the discarded voice. Test both voice and data at every phase.
void stalledBlock(unsigned offset, unsigned blockPhase, unsigned delay,
                  uint8_t type, bool markerLast, uint32_t now) {
  Filter f; Filter::Block b;
  f.reset();
  auto send = [&](unsigned phase, uint32_t time) {
    uint8_t frame[12] = {uint8_t(phase),1,2,3,4,5,6,7,8,
                         uint8_t(type ^ 0x70),0x29,0xf5};
    if (!phase) { frame[9]=0x55; frame[10]=0x2d; frame[11]=0x16; }
    assert(f.push((phase+offset)%21,frame,time));
  };
  for (unsigned phase=0;phase<blockPhase;++phase) {
    send(phase,now);
    f.pop(now,b);
  }
  // Sync blocks start with sync; ordinary pairs can arrive in either order.
  send(blockPhase + (markerLast && blockPhase ? 1 : 0),now);
  for (unsigned dt=0;dt<delay;++dt)
    assert(!f.pop(now+dt,b,false,80));
  if (!blockPhase) {
    send(1,now+delay);
    assert(!f.pop(now+delay,b,false,80)); // Do not expire inside the burst.
    send(2,now+delay);
  } else {
    send(blockPhase + (markerLast ? 0 : 1),now+delay);
  }
  assert(f.pop(now+delay,b,false,80));
  assert(b.kind == (type==0x94 ? Filter::Kind::FastData : Filter::Kind::Voice));
  assert(b.reason == Filter::Reason::None);
  assert(b.count == (blockPhase ? 2 : 3) && b.present == (1U<<b.count)-1);
  for (unsigned i=0;i<b.count;++i) assert(b.ambe[i][0]==blockPhase+i);
  assert(!f.pop(now+delay,b));
}

void boundedStall() {
  Filter f; Filter::Block b;
  prime(f);
  push(f,4,0x66,100); // Continuation alone; no trustworthy mini-header.
  assert(!f.pop(339,b,false,80));
  assert(f.pop(340,b,false,80) && b.kind==Filter::Kind::Unknown);
  assert(b.reason==Filter::Reason::MissingMarker && b.present==2);
  prime(f); push(f,4,0x66,100);
  push(f,5,0x66,240); // A later block stops the extra stalled-block wait.
  assert(f.pop(240,b,false,80) && b.kind==Filter::Kind::Unknown);
  prime(f); push(f,3,0x66,100);
  assert(!f.pop(339,b,false,80));
  assert(f.pop(340,b,false,80) && b.kind==Filter::Kind::Voice && b.present==1);
  prime(f); push(f,4,0x66,100);
  assert(f.pop(100,b,true,80) && b.kind==Filter::Kind::Unknown);
}

int main() {
  boundedStall();
  for (unsigned offset=0;offset<21;++offset)
    for (unsigned phase : {0U,3U,19U})
      for (unsigned delay : {81U,125U,140U,220U,239U})
        for (uint8_t type : {0x66,0x94})
          for (bool markerLast : {false,true})
            for (uint32_t clock : {1000U,UINT32_MAX-100U})
              stalledBlock(offset,phase,delay,type,markerLast,clock);
  unanchoredAndFalseSync();
  for (unsigned offset = 0; offset < 21; ++offset) offsetMissingMarker(offset);
  for (unsigned offset = 0; offset < 21; ++offset)
    for (bool data : {false, true})
      for (bool reorder : {false, true})
        for (uint32_t clock : {1000U, UINT32_MAX - 100U})
          offsetCycles(offset, data, reorder, clock);
  Filter f;
  Filter::Block b;
  // Active-stream adaptive budget retains an out-of-order frame which the
  // default 60 ms classification deadline would already have discarded.
  prime(f); push(f,3,0x66,0);
  for(unsigned seq=5;seq<=9;++seq) push(f,seq,0x66,(seq-3)*10);
  assert(!f.pop(70,b,false,120));
  push(f,4,0x66,80);
  assert(f.pop(80,b,false,120) && b.present==3 && b.sequence==3);

  // Every valid slow-data type retains the voice, including DPRS and text.
  for (auto type : {0x31, 0x35, 0x40, 0x43, 0x51, 0x55, 0x66, 0xc2}) {
    f.reset();
    push(f, 0, type, 0);
    assert(!f.pop(0, b));
    push(f, 2, type, 20);
    assert(!f.pop(20, b));
    push(f, 1, type, 25);
    assert(f.pop(25, b));
    assert(b.kind == Filter::Kind::Voice && b.count == 3 && b.present == 7);
    assert(b.ambe[0][4] ==
           0x66); // Legitimate voice must remain byte-identical.
    assert(!f.pop(25, b));
  }
  for (auto type : {0x81, 0x8f, 0x90, 0x94, 0x9c}) {
    f.reset();
    push(f, 0, type, 0);
    push(f, 1, type, 20);
    assert(!f.pop(20, b));
    push(f, 2, type, 40);
    assert(f.pop(40, b));
    assert(b.kind == Filter::Kind::FastData && b.count == 3);
  }
  // Wrong block lengths and reserved mini-headers are silent, not speech.
  for (auto type :
       {0x00, 0x30, 0x36, 0x44, 0x50, 0x56, 0x80, 0x95, 0x9c, 0xc3}) {
    prime(f);
    push(f, 3, type, 0);
    push(f, 4, type, 20);
    assert(f.pop(20, b));
    assert(b.kind == Filter::Kind::Unknown);
    assert(b.reason == Filter::Reason::InvalidMarker);
  }
  // A lost mini-header cannot leak the adjacent picture slots to AMBE.
  f.reset();
  push(f, 0, 0x94, 0);
  push(f, 2, 0x94, 20);
  assert(!f.pop(239, b));
  assert(f.pop(240, b) && b.kind == Filter::Kind::Unknown);
  push(f, 3, 0x66, 260);
  push(f, 4, 0x66, 280);
  assert(f.pop(280, b) && b.kind == Filter::Kind::Voice);
  // Partial final voice blocks retain known voice; unknown sync-only tails do
  // not.
  prime(f);
  push(f, 3, 0x66, 0);
  assert(f.pop(0, b, true));
  assert(b.kind == Filter::Kind::Voice && b.present == 1);
  f.reset();
  push(f, 0, 0x66, 0);
  assert(f.pop(0, b, true));
  assert(b.kind == Filter::Kind::Unknown);
  // Sequence/clock wrap, reordering, duplicate rejection, alternating modes.
  f.reset();
  uint32_t now = UINT32_MAX - 100;
  unsigned frames = 0;
  for (unsigned cycle = 0; cycle < 10; ++cycle) {
    for (unsigned seq = 0; seq < 21; ++seq) {
      push(f, seq, cycle % 2 ? 0x94 : 0x66, now);
      now += 20;
      if (f.pop(now, b)) {
        assert(b.kind ==
               (cycle % 2 ? Filter::Kind::FastData : Filter::Kind::Voice));
        frames += b.count;
      }
    }
  }
  assert(frames == 210);
  uint8_t duplicate[12]{};
  assert(!f.push(20, duplicate, now));
  std::cout
      << "D-STAR block filter: 21 sync offsets, bounded late entry, false sync, Fast Data, DPRS/text voice, "
         "bounded stalled-block recovery, loss, reordering, reserved types, end, clock/sequence wrap passed\n";
}
