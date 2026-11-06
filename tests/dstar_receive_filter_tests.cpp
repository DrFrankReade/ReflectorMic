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
int main() {
  Filter f;
  Filter::Block b;
  // Active-stream adaptive budget retains an out-of-order frame which the
  // default 60 ms classification deadline would already have discarded.
  f.reset(); push(f,3,0x66,0);
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
    f.reset();
    push(f, 3, type, 0);
    push(f, 4, type, 20);
    assert(f.pop(20, b));
    assert(b.kind == Filter::Kind::Unknown);
  }
  // A lost mini-header cannot leak the adjacent picture slots to AMBE.
  f.reset();
  push(f, 0, 0x94, 0);
  push(f, 2, 0x94, 20);
  assert(!f.pop(59, b));
  assert(f.pop(60, b) && b.kind == Filter::Kind::Unknown);
  push(f, 3, 0x66, 80);
  push(f, 4, 0x66, 100);
  assert(f.pop(100, b) && b.kind == Filter::Kind::Voice);
  // Partial final voice blocks retain known voice; unknown sync-only tails do
  // not.
  f.reset();
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
      << "D-STAR block filter: sync lookahead, Fast Data, DPRS/text voice, "
         "loss, reordering, reserved types, end, clock/sequence wrap passed\n";
}
