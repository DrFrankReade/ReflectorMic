#include "audio_engine.h"
#include "audio_settings.h"
#include "runtime_diagnostics.h"
#include <cstring>
using namespace audio_settings;

#include <esp_heap_caps.h>

bool AudioEngine::begin(uint8_t speakerVolume, float micGain) {
  speakerVolume_ = speakerVolume;
  micGain_ = micGain;
  if (!hardware_.begin()) {
    initializationError_ = hardware_.diagnostic();
    return false;
  }
  if (!codec_.begin()) {
    initializationError_ = "MBELIB-neo codec initialization failed";
    return false;
  }
  rxQueue_ = xQueueCreate(RX_QUEUE_FRAMES, sizeof(RxItem));
  txQueue_ = xQueueCreate(TX_QUEUE_FRAMES, sizeof(TxItem));
  captureStorage_ = static_cast<CapturedPcm *>(heap_caps_malloc(
      (ECHO_CAPTURE_FRAMES + 1) * sizeof(CapturedPcm),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rxQueue_ || !txQueue_ || !captureStorage_) {
    initializationError_ = "unable to allocate audio queues";
    return false;
  }
  captureBuffer_.reset(captureStorage_, ECHO_CAPTURE_FRAMES);
#if REFLECTORMIC_DIAGNOSTICS
  initializeTrace();
#endif
  // This small core-1 task keeps microphone requests queued independently of
  // slow codec calls. It blocks between 20 ms frames and never runs the codec.
  if (xTaskCreatePinnedToCore(captureTaskEntry, "reflectormic-capture", 5120,
                             this, 3, &captureTask_, 1) != pdPASS) {
    initializationError_ = "unable to create microphone capture task";
    return false;
  }
  // MBELIB-neo's floating-point spectral analysis owns the largest stack frame.
  // Keep it isolated from Arduino loop(), HTTP, reflector UDP, and I2S work.
  // Core 1 owns Arduino loop(), PTT, HTTP and reflector UDP. The floating-point
  // codec runs on core 0 at a modest priority; Wi-Fi and the higher-priority
  // I2S DMA workers can still preempt it there.
  if (xTaskCreatePinnedToCore(taskEntry, "reflectormic-codec",
                              AUDIO_CODEC_STACK_BYTES, this,
                              AUDIO_CODEC_PRIORITY, &task_,
                              AUDIO_CODEC_CORE) != pdPASS) {
    initializationError_ = "unable to create audio worker task";
    return false;
  }
  initializationError_ = "";
  return true;
}

void AudioEngine::setLevels(uint8_t speakerVolume, float micGain) {
  speakerVolume_ = speakerVolume;
  micGain_ = micGain;
  // The audio worker applies ES8311 gain. Never do I2C or change mixer state
  // on the HTTP task, including while a cold-start silence barrier is active.
}

bool AudioEngine::startTransmit(bool echoCapture) {
  if (!task_ || !captureTask_ || !txQueue_ || codecTestBusy() || captureBusy_)
    return false;
  // Only the codec worker switches ES8311/I2S directions. HTTP/PTT publish a
  // request instead of racing a decoder that is still writing speaker data.
  speakerShutdownPending_ = false;
  rxEndRequested_ = false;
  toneRequested_ = false;
  xQueueReset(txQueue_);
  echoCapture_ = echoCapture;
  captureBuffer_.reset(captureStorage_, echoCapture ? ECHO_CAPTURE_FRAMES
                                                  : LIVE_CAPTURE_FRAMES);
  pcmFrames_ = 0;
  capturedPcmFrames_ = captureDrops_ = staleCaptureDrops_ = 0;
  encodedFrames_ = 0;
  micStartFailures_ = 0;
  micRecordFailures_ = 0;
  encodeFailures_ = 0;
  queueDrops_ = 0;
  encodeOverruns_ = 0;
  lastEncodeUs_ = 0;
  maxEncodeUs_ = 0;
  playbackFailures_ = 0;
  lastPeak_ = 0;
  rxStreamActive_ = false;
  if (rxQueue_) xQueueReset(rxQueue_);
  // Publish the request after all state is initialized.
  txRequested_ = true;
  captureBusy_ = true;  // Also covers PTT released before the worker starts.
  return true;
}

String AudioEngine::captureDiagnostic() const {
  if (!initializationError_.isEmpty()) return initializationError_;
  if (micStartFailures_) return hardware_.diagnostic();
  if (micRecordFailures_ && !pcmFrames_) return "microphone returned no PCM";
  if (encodeFailures_ && !encodedFrames_) return "MBELIB-neo encoder rejected PCM";
#if REFLECTORMIC_DIAGNOSTICS
  String out = "PCM " + String(pcmFrames_) + ", AMBE " +
               String(encodedFrames_) + ", peak " + String(lastPeak_) +
               ", encode " + String(lastEncodeUs_) + " us (max " +
               String(maxEncodeUs_) + " us)";
  if (encodeOverruns_) out += ", overruns " + String(encodeOverruns_);
  out += ", captured " + String(capturedPcmFrames_) +
         ", pending " + String(captureBuffer_.pending());
  if (captureDrops_) out += ", capture drops " + String(captureDrops_);
  if (staleCaptureDrops_) out += ", stale/released drops " + String(staleCaptureDrops_);
  if (queueDrops_) out += ", queue drops " + String(queueDrops_);
  if (playbackFailures_) out += ", speaker errors " + String(playbackFailures_);
  if (task_) {
    out += ", codec stack free " + String(uxTaskGetStackHighWaterMark(task_)) +
           " B";
  }
  return out;
#else
  return "capture ready";
#endif
}

String AudioEngine::playbackDiagnostic() const {
#if REFLECTORMIC_DIAGNOSTICS
  String out = "RX AMBE " + String(receivedFrames_) + ", decoded " +
               String(decodedFrames_) + ", PCM peak " +
               String(lastPlaybackPeak_) + ", decode " +
               String(lastDecodeUs_) + " us (max " +
               String(maxDecodeUs_) + " us), chunks " +
               String(playbackChunks_);
  if (receivedQueueDrops_) out += ", queue drops " + String(receivedQueueDrops_);
  if (decodeFailures_) out += ", decode errors " + String(decodeFailures_);
  out += "; since boot: peak " + String(maxPlaybackPeak_) +
         ", corrected bits " + String(rxCorrectedErrors_) +
         ", muted " + String(rxMutedFrames_) +
         ", repeated " + String(rxRepeatedFrames_) +
         ", RX restarts " + String(rxRecoveries_.load());
  if (playbackFailures_) out += ", speaker errors " + String(playbackFailures_);
  out += hardware_.amplifierEnabled() ? ", amp on" : ", amp off";
  return out;
#else
  if (decodeFailures_) return "received audio could not be decoded";
  return playbackFailures_ ? "speaker output failed" : "receive ready";
#endif
}

String AudioEngine::errorText() const {
  if (!initializationError_.isEmpty()) return initializationError_;
  if (codecWarmupFailed_) return "codec warm-up failed; retrying";
  if (micStartFailures_ || micRecordFailures_) return "microphone capture failed";
  if (encodeFailures_) return "audio encoding failed";
  if (decodeFailures_) return "received audio could not be decoded";
  if (playbackFailures_) return "speaker output failed";
  return "";
}

void AudioEngine::stopTransmit() {
  txRequested_ = false;
}

bool AudioEngine::popEncodedFrame(uint8_t *ambe) {
  TxItem item;
  if (!txQueue_ || xQueueReceive(txQueue_, &item, 0) != pdTRUE) return false;
  memcpy(ambe, item.ambe, sizeof(item.ambe));
  return true;
}

void AudioEngine::beginReceiveStream() {
  if (!rxQueue_ || txRequested_ || captureBusy_) return;
  speakerShutdownPending_ = false;
  rxEndRequested_ = false;
  xQueueReset(rxQueue_);
  receivedFrames_ = 0;
  receivedQueueDrops_ = 0;
  decodedFrames_ = 0;
  decodeFailures_ = 0;
  playbackChunks_ = 0;
  lastDecodeUs_ = 0;
  maxDecodeUs_ = 0;
  lastPlaybackPeak_ = 0;
  playbackFailures_ = 0;
  RxItem item{};
  item.type = RxItemType::Start;
  xQueueSend(rxQueue_, &item, 0);
  rxStreamActive_ = true;
}

bool AudioEngine::submitReceivedFrame(const uint8_t *ambe) {
  if (!rxQueue_ || txRequested_ || !rxStreamActive_) return false;
  RxItem item{};
  item.type = RxItemType::Frame;
  memcpy(item.ambe, ambe, sizeof(item.ambe));
  if (xQueueSend(rxQueue_, &item, 0) == pdTRUE) {
    receivedFrames_++;
    return true;
  }
  receivedQueueDrops_++;
  return false;
}

bool AudioEngine::endReceiveStream() {
  if (!rxQueue_) return false;
  rxStreamActive_ = false;
  // This flag is consumed only after the FIFO becomes empty, so stream end
  // cannot be lost merely because the receive queue was momentarily full.
  rxEndRequested_ = true;
  return true;
}

bool AudioEngine::playSpeakerTestTone() {
  if (!task_ || !rxQueue_ || codecTestBusy() || txRequested_ || captureBusy_ ||
      playbackBusy()) return false;
  toneRequested_ = true;
  return true;
}

void AudioEngine::startSpeakerTestTone() {
  if (!switchToSpeaker()) {
    toneRequested_ = false;
    return;
  }
  if (captureBusy_ || rxRecoveryRequested_) {
    toneRequested_ = false;  // Cancellation during startup is not a fault.
    return;
  }
  RuntimeDiagnostics::mark(RuntimeStage::QueueingTone);
  // Use the same native tone path as M5Stack's VoiceS3R speaker example. This
  // isolates the 48 kHz I2S/ES8311/amplifier chain from MBELIB-neo and from the
  // decoded-audio resampler.
  const bool queued = M5.Speaker.tone(1000.0f, 500, 0, true);
  if (queued) hardware_.enableSpeakerOutput();
  if (!queued) playbackFailures_++;
  RuntimeDiagnostics::mark(queued ? RuntimeStage::SpeakerActive
                                  : RuntimeStage::Ready);
  speakerShutdownPending_ = true;
  speakerIdleSinceMs_ = 0;
  toneRequested_ = false;
}

void AudioEngine::taskEntry(void *arg) {
  static_cast<AudioEngine *>(arg)->taskLoop();
}

bool AudioEngine::switchToMicrophone() {
  if (micMode_) return true;
  RuntimeDiagnostics::mark(RuntimeStage::StartingMicrophone);
  speakerMode_ = false;  // startMicrophone() stops the speaker even on failure.
  if (!hardware_.startMicrophone()) {
    micStartFailures_++;
    RuntimeDiagnostics::mark(RuntimeStage::Ready);
    Serial.printf("[audio] microphone start failed: %s\n",
                  hardware_.diagnostic().c_str());
    return false;
  }
  speakerMode_ = false;
  micMode_ = true;
  RuntimeDiagnostics::mark(RuntimeStage::MicrophoneActive);
  Serial.println("[audio] microphone active");
  return true;
}

bool AudioEngine::switchToSpeaker() {
  if (speakerMode_) return true;
  RuntimeDiagnostics::mark(RuntimeStage::StartingSpeaker);
  micMode_ = false;
  if (!hardware_.startSpeaker(speakerVolume_)) {
    playbackFailures_++;
    RuntimeDiagnostics::mark(RuntimeStage::Ready);
    Serial.printf("[audio] speaker start failed: %s\n",
                  hardware_.diagnostic().c_str());
    return false;
  }
  micMode_ = false;
  speakerMode_ = true;
  // A slider update may have arrived during the cold-start barrier.
  serviceSpeakerVolume();
  if (!speakerMode_) return false;
  RuntimeDiagnostics::mark(RuntimeStage::SpeakerActive);
  Serial.println("[audio] speaker active");
  return true;
}
