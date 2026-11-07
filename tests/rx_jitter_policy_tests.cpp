#include "rx_jitter_policy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
  for (uint32_t start : {0U, UINT32_MAX - 50}) {
    RxJitterPolicy p;
    uint32_t now = start;
    assert(!p.ready(now, 3, false));
    now += 60;
    assert(p.ready(now, 6, false)); p.played(now);
    assert(!p.ready(now, 6, false));
    // Repeated outages grow the target but cannot exceed 240 ms.
    for (unsigned i=0; i<8; ++i) {
      now += 20;
      assert(!p.ready(now, 0, false));
      now += 100;
      assert(!p.ready(now, 2, false));
      now += 140;
      assert(p.ready(now, 2, false)); p.played(now);
      assert(p.targetFrames() <= 12);
    }
    assert(p.targetFrames()==12 && p.refills()==8);
    // A suspended network task cannot issue an overdue catch-up burst.
    now += 500;
    assert(p.ready(now, 6, false)); p.played(now);
    assert(!p.ready(now, 6, false));
    for (unsigned i=0;i<8;++i) { now+=10000; assert(p.relax(now)); }
    assert(p.targetFrames()==4 && !p.relax(now+10000));
    p.reset();
    assert(!p.concealReady(now)); // Never invent speech during initial prefill.
    assert(p.ready(now, 1, true)); // Short final tail bypasses startup.
    p.played(now);
    assert(!p.ready(now, 1, true)); // End still respects frame pacing.
  }
  for (uint32_t start : {1000U, UINT32_MAX - 50}) {
    RxJitterPolicy p;
    uint32_t now = start;
    assert(p.ready(now,6,false)); p.played(now);
    now += 20;
    assert(!p.ready(now,0,false));
    for (unsigned i=0;i<4;++i) {
      assert(p.concealReady(now)); p.concealed(now);
      assert(!p.concealReady(now)); // No same-tick catch-up burst.
      assert(!p.concealReady(now+19)); now+=20;
    }
    assert(!p.concealReady(now) && !p.concealReady(now+1000));
    assert(p.ready(now,12,false)); p.played(now);
    now+=20; assert(!p.ready(now,0,false));
    assert(p.concealReady(now)); p.concealed(now);
    // A full refill one millisecond later still waits for the next deadline.
    assert(!p.ready(now+1,12,false));
    assert(!p.concealReady(now+1));
    assert(p.ready(now+20,12,false)); p.played(now+20);
    p.reset(); assert(!p.concealReady(now+2000));
  }
  puts("Adaptive RX: bounds, refill deadlines, decay, pacing, end and timer wrap passed");
}
