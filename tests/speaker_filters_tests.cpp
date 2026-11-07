#include "speaker_filters.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <random>

constexpr double PI = 3.14159265358979323846;
static double gain(int hz, int low, int high, int slope = 6) {
  SpeakerFilters f;
  const SpeakerFilterSettings s{true, uint16_t(low), uint16_t(high), uint8_t(slope)};
  double energy = 0, input = 0;
  for (int n = 0; n < 16000; ++n) {
    int16_t x = int16_t(10000 * std::sin(2*PI*hz*n/8000));
    const auto original = x;
    f.process(&x, 1, s.packed());
    if (n >= 8000) { energy += double(x)*x; input += double(original)*original; }
  }
  return std::sqrt(energy/input);
}
int main() {
  SpeakerFilters f;
  SpeakerFilterSettings s;
  std::mt19937 rng(42);
  std::array<int16_t, 160> data{};
  for (auto& x : data) x = int16_t(rng());
  const auto original = data;
  f.process(data.data(), data.size(), s.packed());
  assert(data == original); // No quantization or gain change in bypass.
  assert(SpeakerFilterSettings::unpack(s.packed()).packed() == s.packed());
  for (int low : {50,200,800}) for (int high : {1000,3200,3800}) {
    for (int hz : {50,200,600,1000,2000,3200,3800}) {
      const double w = std::tan(PI*hz/8000);
      const double h = std::tan(PI*low/8000), l = std::tan(PI*high/8000);
      const double expected = w/std::hypot(w,h) * l/std::hypot(w,l);
      assert(std::abs(gain(hz,low,high,6)-expected) < 0.001);
      assert(std::abs(gain(hz,low,high,12)-expected*expected) < 0.001);
    }
  }
  const uint32_t legacy = 200U | (3200U << 10) | (1U << 22);
  assert(SpeakerFilterSettings::unpack(legacy).slopeDb == 6);
  for (uint8_t slope : {6,12}) {
    SpeakerFilterSettings stored(true,800,1000,slope);
    assert(SpeakerFilterSettings::unpack(stored.packed()).slopeDb == slope);
  }
  assert(!SpeakerFilterSettings(true,200,3200,9).valid());
  // A sustained level exposes switching clicks that a sine can conceal.
  s.enabled = true;
  int previous = 10000, maxStep = 0;
  for (int n = 0; n < 8000; ++n) {
    if (n == 1600) { s.lowCutHz=800; s.highCutHz=1000; }
    if (n == 2000) s.slopeDb=12;
    if (n == 2400) s.slopeDb=6;
    if (n == 3200) s.enabled=false;
    int16_t x=10000;
    f.process(&x,1,s.packed());
    maxStep=std::max(maxStep,std::abs(int(x)-previous)); previous=x;
    if (n > 4000) assert(x == 10000);
  }
  assert(maxStep < 100);
  {
    SpeakerFilters switching, unchanged;
    SpeakerFilterSettings settings(true,200,1000);
    double deltaEnergy=0;
    for (int n=0;n<16000;++n) {
      int16_t a=int16_t(12000*std::sin(2*PI*1500*n/8000)), b=a;
      if(n==8000) settings.slopeDb=12;
      switching.process(&a,1,settings.packed());
      unchanged.process(&b,1,SpeakerFilterSettings(true,200,1000).packed());
      if(n==8000) assert(std::abs(int(a)-b)<60);
      if(n>9000) deltaEnergy+=(double(a)-b)*(double(a)-b);
    }
    assert(deltaEnergy > 1e9); // Slope switch has a substantial audible effect.
  }
  // Filter memory must not leak across stations or recovery.
  f.reset(); SpeakerFilters fresh;
  s.enabled=true;
  auto a=original,b=original;
  f.process(a.data(),a.size(),s.packed());
  fresh.process(b.data(),b.size(),s.packed());
  assert(a==b);
  // Extreme full-scale inputs and fast retuning stay bounded and recover to silence.
  for (int n=0;n<1000;++n) {
    s={bool(n%3),uint16_t(50+rng()%751),uint16_t(1000+rng()%2801),uint8_t((rng()&1)?6:12)};
    for(auto& x:data) x=(rng()&1)?32767:-32768;
    f.process(data.data(),data.size(),s.packed());
  }
  s={true,200,3200};
  for(int n=0;n<100;++n) { data.fill(0); f.process(data.data(),data.size(),s.packed()); }
  for(auto x:data) assert(x==0);
  puts("Speaker filters: bypass, response, smooth retuning, reset and stress passed");
}
