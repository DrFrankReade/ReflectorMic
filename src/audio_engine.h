#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "dstar_codec.h"
#include "pcm_capture_buffer.h"
#include "speaker_filters.h"
#include "voice_s3r_audio.h"
#include "build_features.h"
#include "diagnostic_counter.h"
#if REFLECTORMIC_DIAGNOSTICS
#include "rx_audio_trace.h"
#endif

class AudioEngine {
 public:
  bool begin(uint8_t speakerVolume, float micGain);
  void setLevels(uint8_t speakerVolume, float micGain);
  String speakerFilterStatus() const;
  // Publish only the speaker level; live preview must not change microphone gain.
  void setSpeakerVolume(uint8_t volume) {
    speakerVolume_.store(volume);
    wakeWorker();
  }
  void setSpeakerFilters(const SpeakerFilterSettings& value) {
    if (value.valid()) {
      speakerFilterSettings_.store(value.packed());
      wakeWorker();
    }
  }

  // Echo preserves the whole bounded recording; live TX bounds latency.
  bool startTransmit(bool echoCapture = false);
  void stopTransmit();
  bool transmitting() const { return txRequested_; }
  bool captureBusy() const { return captureBusy_; }
  uint32_t capturedPcmFrames() const { return capturedPcmFrames_; }
  uint32_t pendingPcmFrames() const { return captureBuffer_.pending(); }
  UBaseType_t encodedFramesWaiting() const {
    return txQueue_ ? uxQueueMessagesWaiting(txQueue_) : 0;
  }
  bool popEncodedFrame(uint8_t ambe[DSTAR_AMBE_BYTES]);
  String captureDiagnostic() const;
  String playbackDiagnostic() const;
  String errorText() const;
#if REFLECTORMIC_DIAGNOSTICS
  bool requestRxSnapshot() {
    if (!rxTrace_.request()) return false;
    wakeWorker();
    return true;
  }
  const RxTraceSnapshot* rxSnapshot() const { return rxTrace_.snapshot(); }
#endif
  bool requestRxRecovery();
  String codecDiagnostic() const;
  bool requestCodecTest();
  bool codecTestBusy() const {
#if REFLECTORMIC_DIAGNOSTICS
    return !codecReady_ || codecTestState_ == 1 || codecTestState_ == 2;
#else
    return !codecReady_;
#endif
  }
  String hardwareDiagnostic() const { return hardware_.diagnostic(); }

  void beginReceiveStream();
  bool submitReceivedFrame(const uint8_t ambe[DSTAR_AMBE_BYTES], bool live = true);
  // Returns false only when the end marker could not yet be queued. Callers
  // that generate audio faster than real time can retry on their next pass.
  bool endReceiveStream();
  bool receivingAudio() const { return rxStreamActive_; }
  bool playbackBusy() const {
    return rxStreamActive_ || rxEndRequested_ || speakerShutdownPending_ || toneRequested_;
  }
  bool playSpeakerTestTone();
  const char *codecVersion() const { return codec_.version(); }

 private:
  enum class RxItemType : uint8_t { Frame, Start };
  struct RxItem {
    RxItemType type;
    uint32_t queuedMs = 0;
    bool live = true;
    uint8_t ambe[DSTAR_AMBE_BYTES];
  };
  struct TxItem {
    uint8_t ambe[DSTAR_AMBE_BYTES];
  };

  static void taskEntry(void *arg);
  static void captureTaskEntry(void *arg);
  void captureTaskLoop();
  void taskLoop();
  void wakeWorker();
  void waitForIdleWork();
  void warmCodec();
#if REFLECTORMIC_DIAGNOSTICS
  void runCodecTest();
  void initializeTrace();
  void recordReceivedFrame(const RxItem&, const int16_t*, bool, uint32_t, const mbe_process_result&);
#endif
  void processReceiveItem(const RxItem& item);
  bool awaitPlaybackSlot();
  void decodeReceivedFrame(const RxItem& item);
  void captureTransmitSession();
  void encodeCapturedPcm(int16_t pcm[DSTAR_PCM_SAMPLES]);
  bool switchToMicrophone();
  bool switchToSpeaker();
  void finishReceiveStream();
  void serviceSpeakerShutdown();
  void serviceSpeakerVolume();
  void startSpeakerTestTone();
  void flushPlaybackChunk(bool pad);
  void serviceRxDiagnostics();

  // HTTP publishes one coherent settings word. Only the audio worker owns
  // filter history and modifies PCM, before the mixer can retain its pointer.
  std::atomic<uint32_t> speakerFilterSettings_{SpeakerFilterSettings{}.packed()};
  std::atomic<uint32_t> speakerFilterApplied_{UINT32_MAX};
  SpeakerFilters speakerFilters_;
  DStarCodec codec_;
#if REFLECTORMIC_DIAGNOSTICS
  RxAudioTrace rxTrace_;
#endif
  std::atomic<bool> rxRecoveryRequested_{false};
  std::atomic<uint32_t> rxRecoveries_{0};
#if REFLECTORMIC_DIAGNOSTICS
  uint32_t rxTraceStream_ = 0, rxTraceFrame_ = 0;
  DiagnosticCounter<uint16_t> maxPlaybackPeak_{0};
  DiagnosticCounter<> rxCorrectedErrors_, rxMutedFrames_, rxRepeatedFrames_;
#endif
  VoiceS3rAudioHardware hardware_;
  String initializationError_;
  // Run one silent, bounded benchmark on the codec worker after boot. Results
  // are published only after it finishes; no audio is sent to a reflector.
  std::atomic<bool> codecReady_{false};
  std::atomic<bool> codecWarmupFailed_{false};
#if REFLECTORMIC_DIAGNOSTICS
  std::atomic<uint8_t> codecTestState_{1};
  uint32_t testEncodeMaxUs_ = 0;
  uint32_t testDecodeMaxUs_ = 0;
  uint32_t testEncodeSumUs_ = 0;
  uint32_t testDecodeSumUs_ = 0;
  uint32_t testFrames_ = 0;
  uint32_t testFailures_ = 0;
#endif
  QueueHandle_t rxQueue_ = nullptr;
  QueueHandle_t txQueue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  DiagnosticCounter<> idleWaits_, idleWakeups_;
  TaskHandle_t captureTask_ = nullptr;
  CapturedPcm *captureStorage_ = nullptr;
  PcmCaptureBuffer captureBuffer_;
  std::atomic<bool> txRequested_{false};
  std::atomic<bool> captureBusy_{false};
  std::atomic<bool> captureProducerBusy_{false};
  bool echoCapture_ = false;
  std::atomic<bool> toneRequested_{false};
  std::atomic<bool> rxStreamActive_{false};
  std::atomic<bool> rxEndRequested_{false};
  std::atomic<bool> speakerShutdownPending_{false};
  std::atomic<uint8_t> speakerVolume_{160};
  std::atomic<float> micGain_{1.0f};
  std::atomic<bool> micMode_{false};
  std::atomic<bool> speakerMode_{false};
  std::atomic<uint32_t> pcmFrames_{0};
  std::atomic<uint32_t> capturedPcmFrames_{0};
  DiagnosticCounter<uint32_t> captureDrops_{0};
  DiagnosticCounter<uint32_t> staleCaptureDrops_{0};
  std::atomic<uint32_t> encodedFrames_{0};
  std::atomic<uint32_t> micStartFailures_{0};
  std::atomic<uint32_t> micRecordFailures_{0};
  std::atomic<uint32_t> encodeFailures_{0};
  DiagnosticCounter<uint32_t> queueDrops_{0};
  DiagnosticCounter<uint32_t> encodeOverruns_{0};
  DiagnosticCounter<uint32_t> lastEncodeUs_{0};
  DiagnosticCounter<uint32_t> maxEncodeUs_{0};
  std::atomic<uint32_t> playbackFailures_{0};
  DiagnosticCounter<uint16_t> lastPeak_{0};
  DiagnosticCounter<uint32_t> receivedFrames_{0};
  DiagnosticCounter<uint32_t> receivedQueueDrops_{0};
  DiagnosticCounter<uint32_t> decodedFrames_{0};
  std::atomic<uint32_t> decodeFailures_{0};
  DiagnosticCounter<uint32_t> playbackChunks_{0};
  DiagnosticCounter<uint32_t> lastDecodeUs_{0};
  DiagnosticCounter<uint32_t> maxDecodeUs_{0};
  DiagnosticCounter<uint16_t> lastPlaybackPeak_{0};
  int16_t playback_[3][DSTAR_PCM_SAMPLES * 2]{};
  uint8_t playbackBuffer_ = 0;
  uint8_t playbackFrames_ = 0;
  uint32_t speakerIdleSinceMs_ = 0;
};
