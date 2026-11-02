#include "runtime_diagnostics.h"

#if REFLECTORMIC_DIAGNOSTICS
#include <esp_attr.h>
#include <esp_rom_sys.h>
#include <esp_system.h>

namespace {

constexpr uint32_t RTC_MAGIC = 0x524D4943;  // "RMIC"
RTC_DATA_ATTR uint32_t retainedMagic = 0;
RTC_DATA_ATTR uint32_t retainedStage = 0;
RTC_DATA_ATTR uint32_t retainedBootCount = 0;

esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
soc_reset_reason_t romResetReasonCpu0 = RESET_REASON_CHIP_POWER_ON;
soc_reset_reason_t romResetReasonCpu1 = RESET_REASON_CHIP_POWER_ON;
RuntimeStage previousStage = RuntimeStage::Unknown;
bool previousStageValid = false;
uint32_t bootCount = 0;

const char *resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external reset";
    case ESP_RST_SW: return "software reset";
    case ESP_RST_PANIC: return "panic/exception";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO reset";
    default: return "unknown";
  }
}

// ESP-IDF 4.4 collapses several ESP32-S3-specific reset causes into
// ESP_RST_UNKNOWN. The ROM keeps the precise CPU reason, including the native
// USB reset causes that can look like a firmware boot loop on this board.
const char *romResetReasonText(soc_reset_reason_t reason) {
  switch (static_cast<int>(reason)) {
    case 0: return "no reason";
    case 1: return "power-on";
    case 3: return "software system reset";
    case 5: return "deep-sleep wake";
    case 7: return "timer-group-0 watchdog (system)";
    case 8: return "timer-group-1 watchdog (system)";
    case 9: return "RTC watchdog (system)";
    case 10: return "intrusion reset";
    case 11: return "timer-group-0 watchdog (CPU)";
    case 12: return "software CPU reset";
    case 13: return "RTC watchdog (CPU)";
    case 15: return "brownout";
    case 16: return "RTC watchdog (chip)";
    case 17: return "timer-group-1 watchdog (CPU)";
    case 18: return "super watchdog";
    case 19: return "clock glitch";
    case 20: return "eFuse reset";
    case 21: return "USB-UART reset";
    case 22: return "USB-JTAG reset";
    case 23: return "power glitch";
    default: return "unrecognized";
  }
}

const char *stageText(RuntimeStage stage) {
  switch (stage) {
    case RuntimeStage::Booting: return "booting";
    case RuntimeStage::Ready: return "ready/idle";
    case RuntimeStage::StartingMicrophone: return "starting microphone";
    case RuntimeStage::MicrophoneActive: return "microphone active";
    case RuntimeStage::QueueingCapture: return "queueing microphone capture";
    case RuntimeStage::Capturing: return "capturing/encoding audio";
    case RuntimeStage::StartingSpeaker: return "starting speaker";
    case RuntimeStage::SpeakerActive: return "speaker active";
    case RuntimeStage::QueueingTone: return "queueing speaker test tone";
    case RuntimeStage::QueueingPlayback: return "queueing decoded playback";
    case RuntimeStage::StoppingAudio: return "stopping audio";
    case RuntimeStage::SpeakerStopTimeout: return "speaker driver shutdown timed out";
    default: return "unavailable";
  }
}

}  // namespace

void RuntimeDiagnostics::beginBoot() {
  bootResetReason = esp_reset_reason();
  romResetReasonCpu0 = esp_rom_get_reset_reason(0);
  romResetReasonCpu1 = esp_rom_get_reset_reason(1);
  previousStageValid = retainedMagic == RTC_MAGIC;
  if (previousStageValid) {
    previousStage = static_cast<RuntimeStage>(retainedStage);
    bootCount = ++retainedBootCount;
  } else {
    retainedBootCount = 1;
    bootCount = 1;
  }
  retainedMagic = RTC_MAGIC;
  retainedStage = static_cast<uint32_t>(RuntimeStage::Booting);
}

void RuntimeDiagnostics::mark(RuntimeStage stage) {
  retainedStage = static_cast<uint32_t>(stage);
}

String RuntimeDiagnostics::summary() {
  String out = "Last reset: ";
  out += resetReasonText(bootResetReason);
  out += "; ROM CPU0: ";
  out += romResetReasonText(romResetReasonCpu0);
  out += " (" + String(static_cast<int>(romResetReasonCpu0)) + ')';
  out += "; CPU1: ";
  out += romResetReasonText(romResetReasonCpu1);
  out += " (" + String(static_cast<int>(romResetReasonCpu1)) + ')';
  out += "; boot #" + String(bootCount) + " since power cycle";
  out += "; previous stage: ";
  out += previousStageValid ? stageText(previousStage) : "unavailable";
  return out;
}

#endif
