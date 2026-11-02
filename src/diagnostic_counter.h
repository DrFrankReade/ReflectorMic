#pragma once
#include "build_features.h"
#include <atomic>
#include <cstdint>

// Metrics never control the state machine. In lean builds updates become
// no-ops; enabled builds publish scalar snapshots safely across CPU cores.
template<typename T = uint32_t> class DiagnosticCounter {
 public:
  DiagnosticCounter(T value = 0) {
#if REFLECTORMIC_DIAGNOSTICS
    value_.store(value, std::memory_order_relaxed);
#else
    (void)value;
#endif
  }
  operator T() const {
#if REFLECTORMIC_DIAGNOSTICS
    return value_.load(std::memory_order_relaxed);
#else
    return 0;
#endif
  }
  DiagnosticCounter& operator=(T value) {
#if REFLECTORMIC_DIAGNOSTICS
    value_.store(value, std::memory_order_relaxed);
#else
    (void)value;
#endif
    return *this;
  }
  DiagnosticCounter& operator=(const DiagnosticCounter& other) { return *this = T(other); }
  void operator++() { *this += 1; }
  void operator++(int) { *this += 1; }
  void operator+=(T amount) {
#if REFLECTORMIC_DIAGNOSTICS
    value_.fetch_add(amount, std::memory_order_relaxed);
#else
    (void)amount;
#endif
  }
 private:
#if REFLECTORMIC_DIAGNOSTICS
  std::atomic<T> value_{0};
#endif
};
