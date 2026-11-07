#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>

#if CONFIG_BT_ENABLED
#include <esp_bt.h>
#endif

#include "app_config.h"
#include "audio_engine.h"
#include "echo_test.h"
#include "firmware_version.h"
#include "reflector_client.h"
#include "runtime_diagnostics.h"
#include "resource_diagnostics.h"
#include "web_server.h"
#include "wifi_manager.h"

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint8_t PTT_PIN = 41;
constexpr uint32_t PTT_DEBOUNCE_MS = 30;

ConfigStore configStore;
WifiManager wifiManager;
ReflectorClient reflector;
AudioEngine audio;
EchoTest echoTest;
ReflectorMicWebServer webServer;

bool pttRawPressed = false;
bool pttStablePressed = false;
bool pttTimeoutLock = false;
uint32_t pttRawChangedMs = 0;
uint32_t txStartedMs = 0;
bool networkTxStopPending = false;
bool bootBeepPending = false;
uint32_t networkTxStopStartedMs = 0;
const char *networkTxStopReason = "completed";

void onIncomingStart(const IncomingCallInfo &info, void *) {
  Serial.printf("[rx] stream=%04X from=%s/%s to=%s rpt1=%s rpt2=%s\n",
                info.streamId, info.from.c_str(), info.suffix.c_str(),
                info.destination.c_str(), info.rpt1.c_str(), info.rpt2.c_str());
  if (!echoTest.active()) audio.beginReceiveStream();
}

void onIncomingFrame(const uint8_t ambe[9], void *) {
  if (!echoTest.active()) audio.submitReceivedFrame(ambe);
}

void onIncomingEnd(void *) {
#if REFLECTORMIC_DIAGNOSTICS
  Serial.printf("[rx] stream ended; network voice=%lu/%lu parse=%lu wrong-stream=%lu no-header=%lu\n",
                static_cast<unsigned long>(reflector.acceptedVoiceFrames()),
                static_cast<unsigned long>(reflector.receivedVoicePackets()),
                static_cast<unsigned long>(reflector.mediaParseFailures()),
                static_cast<unsigned long>(reflector.wrongStreamFrames()),
                static_cast<unsigned long>(reflector.voiceWithoutHeader()));
#endif
  if (!echoTest.active()) audio.endReceiveStream();
}

void stopTransmission(const char *reason) {
  if (echoTest.recording()) {
    echoTest.stopRecording(reason);
    return;
  }
  if (networkTxStopPending) return;
  if (!reflector.transmitting() && !audio.transmitting()) return;
  audio.stopTransmit();
  if (reflector.transmitting()) {
    // The codec worker may be finishing one 20 ms frame and the paced network
    // sender may have a small header-guard backlog. Keep the stream open until
    // those AMBE frames have left, with a bounded fallback if the worker stalls.
    networkTxStopPending = true;
    networkTxStopStartedMs = millis();
    networkTxStopReason = reason;
  } else {
    Serial.printf("[ptt] TX ended locally: %s\n", reason);
  }
}

void startTransmission() {
  if (pttTimeoutLock || networkTxStopPending) return;
  if (echoTest.active()) {
    if (echoTest.armed() && echoTest.startRecording()) {
      Serial.println("[echo] recording started");
    } else if (echoTest.armed()) {
      Serial.printf("[echo] PTT detected but capture did not start: %s\n",
                    echoTest.statusText().c_str());
    }
    return;
  }
  if (!reflector.linked()) {
    Serial.println("[ptt] ignored: reflector is not linked");
    return;
  }
  if (!reflector.beginTransmission()) {
    Serial.println("[ptt] unable to start network stream");
    return;
  }
  if (!audio.startTransmit()) {
    reflector.endTransmission();
    Serial.println("[ptt] unable to start microphone");
    return;
  }
  txStartedMs = millis();
  Serial.printf("[ptt] TX started; timeout=%us\n", configStore.get().txTimeoutSec);
}

void servicePtt() {
  const uint32_t now = millis();
  const bool pressed = digitalRead(PTT_PIN) == LOW;
  if (pressed != pttRawPressed) {
    pttRawPressed = pressed;
    pttRawChangedMs = now;
  }
  if (pressed != pttStablePressed && now - pttRawChangedMs >= PTT_DEBOUNCE_MS) {
    pttStablePressed = pressed;
    if (pressed) {
      startTransmission();
    } else {
      stopTransmission("button released");
      pttTimeoutLock = false;
    }
  }

  if (reflector.transmitting() &&
      // startTransmission() above may spend milliseconds sending headers and
      // then set txStartedMs. The debounce timestamp predates that start;
      // subtracting it would wrap and immediately trip the TX timeout.
      millis() - txStartedMs >= static_cast<uint32_t>(configStore.get().txTimeoutSec) * 1000UL) {
    pttTimeoutLock = true;
    stopTransmission("PTT timeout");
  }
}

void serviceEncodedAudio() {
  echoTest.service();
  if (echoTest.active()) return;
  uint8_t ambe[9];
  uint8_t sent = 0;
  while (reflector.transmitting() && reflector.voiceFrameDue() && sent++ < 2) {
    const bool encoded = audio.popEncodedFrame(ambe);
    // Release stops new capture, but the worker may still be encoding its
    // final frame. Keep sending paced fill frames until it drains as well.
    if (!encoded && !audio.transmitting() && !audio.captureBusy()) break;
    const bool ok = encoded ? reflector.sendVoice(ambe) : reflector.sendSilence();
    if (!ok) {
      stopTransmission("network send failure");
      break;
    }
  }
  if (networkTxStopPending) {
    const bool drained = !audio.captureBusy() && audio.encodedFramesWaiting() == 0;
    const bool expired = millis() - networkTxStopStartedMs >= 1000;
    if (!reflector.transmitting() || drained || expired) {
      if (reflector.transmitting()) reflector.endTransmission();
      Serial.printf("[ptt] TX ended: %s%s\n", networkTxStopReason,
                    expired && !drained ? " (drain timeout)" : "");
#if REFLECTORMIC_DIAGNOSTICS
      Serial.printf("[ptt] %s; network voice=%lu headers=%lu ends=%lu\n",
                    audio.captureDiagnostic().c_str(),
                    static_cast<unsigned long>(reflector.transmittedVoiceFrames()),
                    static_cast<unsigned long>(reflector.transmittedHeaderPackets()),
                    static_cast<unsigned long>(reflector.transmittedEndPackets()));
#endif
      networkTxStopPending = false;
    }
  }
  if (audio.transmitting() && !reflector.transmitting()) {
    audio.stopTransmit();
  }
  if (reflector.transmitting() && !audio.transmitting() &&
      !audio.captureBusy() && !networkTxStopPending) {
    stopTransmission("microphone capture stopped");
  }
}

void serviceDiagnostics() {
#if REFLECTORMIC_DIAGNOSTICS
  static uint32_t lastReportMs = 0;
  const uint32_t now = millis();
  if (now - lastReportMs < 10000) return;
  lastReportMs = now;
  Serial.printf("[status] wifi=%s ip=%s reflector=%s target=%s heap=%u psram=%u\n",
                wifiManager.connected() ? "up" : "down",
                wifiManager.ipAddress().c_str(), reflector.stateText().c_str(),
                reflector.targetText().c_str(), ESP.getFreeHeap(), ESP.getFreePsram());
#endif
}

}  // namespace

void setup() {
  RuntimeDiagnostics::beginBoot();
  ResourceDiagnostics::begin();
  pinMode(PTT_PIN, INPUT_PULLUP);
  Serial.begin(SERIAL_BAUD);
  delay(100);
  Serial.println();
  Serial.println("[boot] ReflectorMic");
  Serial.printf("[boot] firmware=%s\n", REFLECTORMIC_FIRMWARE);
  Serial.printf("[boot] %s\n", RuntimeDiagnostics::summary().c_str());

#if CONFIG_BT_ENABLED
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
#endif

  // ReflectorMic has one known, displayless hardware target. M5Unified's S3R
  // family autodetection can misclassify this unit as AtomS3R-Ext and disable
  // its audio classes, so the audio layer initializes only the documented
  // VoiceS3R bus, pins, and codec. No display/IMU/LED setup is needed here.

  // Arduino-ESP32 LittleFS locates a filesystem partition by label. Our
  // partition table uses its conventional "spiffs" label even though the
  // on-flash format is LittleFS. The old "littlefs" label could never be
  // found by LittleFS.begin(), which also prevented browser uploads.
  bool filesystemMounted = LittleFS.begin(false);
  if (!filesystemMounted) {
    Serial.println("[fs] mount failed; formatting LittleFS partition");
    filesystemMounted = LittleFS.begin(true);
  }
  if (filesystemMounted) {
    Serial.printf("[fs] mounted: %u / %u bytes used\n",
                  LittleFS.usedBytes(), LittleFS.totalBytes());
  } else {
    Serial.println("[fs] LittleFS unavailable after format attempt");
  }
  if (!configStore.begin()) {
    Serial.println("[config] NVS initialization failed");
  }

  const bool audioReady =
      audio.begin(configStore.get().speakerVolume, configStore.get().micGain);
  audio.setSpeakerFilters(configStore.get().speakerFilters);
  bootBeepPending = audioReady;
  if (!audioReady) {
    Serial.printf("[audio] initialization failed: %s\n",
                  audio.captureDiagnostic().c_str());
  } else {
    Serial.printf("[audio] codec=%s\n", audio.codecVersion());
  }
  if (!echoTest.begin(audio)) {
    Serial.println("[echo] PSRAM allocation failed");
  } else if (!audioReady) {
    echoTest.setUnavailable("audio engine unavailable (" +
                            audio.captureDiagnostic() + ")");
  }

  reflector.begin(configStore);
  reflector.setCallbacks(onIncomingStart, onIncomingFrame, onIncomingEnd, nullptr);
  wifiManager.begin(configStore.get());
  webServer.begin(configStore, wifiManager, reflector, audio, echoTest);

  pttRawPressed = pttStablePressed = digitalRead(PTT_PIN) == LOW;
  pttRawChangedMs = millis();
  Serial.printf("[boot] heap=%u psram=%u PTT=GPIO%u\n",
                ESP.getFreeHeap(), ESP.getPsramSize(), PTT_PIN);
  Serial.printf("[boot] Arduino/PTT/network/capture core=%d; codec core=0; I2S defaults\n",
                xPortGetCoreID());
  RuntimeDiagnostics::mark(RuntimeStage::Ready);
}

void loop() {
  ResourceDiagnostics::beginLoop();
  // PTT is serviced before networking and HTTP on every core-1 loop pass.
  // The CPU-intensive codec now runs independently on core 0.
  servicePtt();
  // Queue the UI's bounded 1 kHz tone after warmup, without blocking startup.
  // Skip if user or reflector audio has already started.
  if (bootBeepPending && !audio.codecTestBusy()) {
    bootBeepPending = false;
    if (!pttStablePressed && !echoTest.active() && !reflector.incoming() &&
        !audio.transmitting() && !audio.playbackBusy()) audio.playSpeakerTestTone();
  }
  webServer.setPttPressed(pttStablePressed);
  wifiManager.service();
  reflector.service(wifiManager.connected());
  serviceEncodedAudio();
  webServer.service();

  serviceDiagnostics();
  ResourceDiagnostics::endLoop();
  delay(1);
}
