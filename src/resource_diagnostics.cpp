#include "resource_diagnostics.h"
#if REFLECTORMIC_DIAGNOSTICS
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

namespace ResourceDiagnostics {
namespace {
uint32_t imageBytes, partitionBytes, windowStart, loopStart, busyUs;
uint32_t loopActivePermille, maxLoopUs;
} // namespace
void begin() {
  // Image inspection is done once at startup, never on an HTTP/audio pass.
  imageBytes = ESP.getSketchSize();
  const auto *partition = esp_ota_get_running_partition();
  partitionBytes = partition ? partition->size : 0;
  windowStart = micros();
}
void beginLoop() { loopStart = micros(); }
void endLoop() {
  const uint32_t now = micros(), elapsed = now - loopStart;
  busyUs += elapsed;
  if (elapsed > maxLoopUs)
    maxLoopUs = elapsed;
  const uint32_t window = now - windowStart;
  if (window >= 1000000U) {
    loopActivePermille = static_cast<uint64_t>(busyUs) * 1000U / window;
    busyUs = 0;
    windowStart = now;
  }
}
String snapshotJson() {
  constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  String out;
  out.reserve(512);
  out = "{\"cpuMHz\":" + String(ESP.getCpuFreqMHz());
  out += ",\"networkCore\":" + String(xPortGetCoreID());
  out += ",\"codecCore\":0,\"uptimeMs\":" + String(millis());
  // This is loop wall time excluding delay(1), NOT total per-core CPU usage.
  // Includes preemption; Arduino's SDK disables FreeRTOS runtime statistics.
  out += ",\"loopActivePermille\":" + String(loopActivePermille);
  out += ",\"loopMaxUs\":" + String(maxLoopUs);
  out +=
      ",\"loopStackFreeBytes\":" + String(uxTaskGetStackHighWaterMark(nullptr));
  out += ",\"internalFreeBytes\":" + String(heap_caps_get_free_size(internal));
  out += ",\"internalMinimumBytes\":" +
         String(heap_caps_get_minimum_free_size(internal));
  out += ",\"internalLargestBytes\":" +
         String(heap_caps_get_largest_free_block(internal));
  out += ",\"psramTotalBytes\":" + String(ESP.getPsramSize());
  out += ",\"psramFreeBytes\":" + String(heap_caps_get_free_size(external));
  out += ",\"psramLargestBytes\":" +
         String(heap_caps_get_largest_free_block(external));
  out += ",\"flashBytes\":" + String(ESP.getFlashChipSize());
  out += ",\"appBytes\":" + String(imageBytes);
  out += ",\"appPartitionBytes\":" + String(partitionBytes);
  return out + '}';
}
} // namespace ResourceDiagnostics
#endif
