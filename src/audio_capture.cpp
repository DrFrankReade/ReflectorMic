#include "audio_engine.h"

#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>

#include "runtime_diagnostics.h"
#include "esp_dsp_trial.h"
#include "encoder_profile.h"
#include "audio_settings.h"

using namespace audio_settings;
namespace {

int16_t scaleSample(int16_t sample, float gain) {
  const int32_t scaled = static_cast<int32_t>(lroundf(static_cast<float>(sample) * gain));
  return static_cast<int16_t>(constrain(scaled, -32768L, 32767L));
}

}  // namespace

void AudioEngine::captureTaskEntry(void *arg) {
  static_cast<AudioEngine *>(arg)->captureTaskLoop();
}

void AudioEngine::captureTaskLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int16_t pcm[2][DSTAR_PCM_SAMPLES];
    uint8_t recordingBuffer = 0;
    CapturedPcm frame;

    RuntimeDiagnostics::mark(RuntimeStage::QueueingCapture);
    uint8_t queued = 0;
    uint32_t requested = 0;
    // Keep BOTH M5 recording slots populated. Waiting for isRecording()==0
    // between frames makes its worker discard partial DMA input and restart.
    for (uint8_t i = 0; i < 2 && txRequested_; ++i) {
      if (!M5.Mic.record(pcm[i], DSTAR_PCM_SAMPLES, SAMPLE_RATE)) {
        micRecordFailures_++;
        txRequested_ = false;
        break;
      }
      ++queued;
      ++requested;
    }
    while (queued) {
      while (M5.Mic.isRecording() >= queued) vTaskDelay(1);
      const uint8_t completedBuffer = recordingBuffer;
      recordingBuffer ^= 1U;
      --queued;
      frame.completedMs = millis();
      capturedPcmFrames_++;
      if (echoCapture_ && capturedPcmFrames_ >= ECHO_CAPTURE_FRAMES)
        txRequested_ = false;
      memcpy(frame.samples, pcm[completedBuffer], sizeof(frame.samples));
      if (!captureBuffer_.push(frame)) captureDrops_++;

      // Keep I2S draining even while encoding falls behind. Only this task
      // calls record(); the codec task receives immutable copies in PSRAM.
      if (txRequested_ && (!echoCapture_ || requested < ECHO_CAPTURE_FRAMES)) {
        if (M5.Mic.record(pcm[completedBuffer], DSTAR_PCM_SAMPLES, SAMPLE_RATE)) {
          ++queued;
          ++requested;
        } else {
          micRecordFailures_++;
          txRequested_ = false;
        }
      }
      RuntimeDiagnostics::mark(RuntimeStage::Capturing);
    }
    captureProducerBusy_ = false;
  }
}

void AudioEngine::captureTransmitSession() {
  speakerShutdownPending_ = false;
  rxEndRequested_ = false;
  playbackFrames_ = 0;
  if (!txRequested_ || !switchToMicrophone()) {
    hardware_.stopAll();
    speakerMode_ = micMode_ = false;
    txRequested_ = false;
    captureBusy_ = false;
    return;
  }
  codec_.resetEncoder();
  captureProducerBusy_ = true;
  xTaskNotifyGive(captureTask_);

  CapturedPcm frame;
  for (;;) {
    // Read producer completion before checking the FIFO. Its release-store
    // publishes the final frame; checking in the reverse order can miss it.
    const bool producing = captureProducerBusy_.load();
    if (captureBuffer_.pop(frame)) {
      if (!echoCapture_ && (!txRequested_ ||
          millis() - frame.completedMs > LIVE_CAPTURE_MAX_AGE_MS)) {
        staleCaptureDrops_++;
      } else {
        encodeCapturedPcm(frame.samples);
      }
    } else if (!producing) {
      break;
    }
    // Let Wi-Fi and the idle/watchdog task run even with a full PCM backlog.
    vTaskDelay(1);
  }

  RuntimeDiagnostics::mark(RuntimeStage::StoppingAudio);
  hardware_.stopMicrophone();
  micMode_ = false;
  RuntimeDiagnostics::mark(RuntimeStage::Ready);
  captureBusy_ = false;
}

void AudioEngine::encodeCapturedPcm(int16_t *pcm) {
  const float gain = micGain_;
  if (fabsf(gain - 1.0f) > 0.001f) {
    for (size_t i = 0; i < DSTAR_PCM_SAMPLES; ++i) {
      pcm[i] = scaleSample(pcm[i], gain);
    }
  }
#if REFLECTORMIC_DIAGNOSTICS
  uint16_t peak = 0;
  for (size_t i = 0; i < DSTAR_PCM_SAMPLES; ++i) {
    const int16_t sample = pcm[i];
    const uint16_t magnitude = sample == INT16_MIN
                                   ? 32768U
                                   : static_cast<uint16_t>(abs(sample));
    if (magnitude > peak) peak = magnitude;
  }
  lastPeak_ = peak;
#endif
  pcmFrames_++;

  TxItem item{};
#if REFLECTORMIC_DIAGNOSTICS
  const uint32_t startedUs = micros();
#endif
  const bool encoded = codec_.encode(pcm, item.ambe);
#if REFLECTORMIC_DIAGNOSTICS
  const uint32_t elapsedUs = micros() - startedUs;
  lastEncodeUs_ = elapsedUs;
  if (elapsedUs > maxEncodeUs_) maxEncodeUs_ = elapsedUs;
  if (elapsedUs > 20000U) encodeOverruns_++;
#endif

  if (!encoded) {
    encodeFailures_++;
    return;
  }
  if (xQueueSend(txQueue_, &item, echoCapture_ ? pdMS_TO_TICKS(250) : 0) == pdTRUE) {
    encodedFrames_++;
  } else {
    queueDrops_++;
  }
}
