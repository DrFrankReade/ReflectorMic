#include "audio_engine.h"
#include "audio_settings.h"
#include "runtime_diagnostics.h"
#include <cstring>
using namespace audio_settings;

void AudioEngine::taskLoop() {
  while (!codecReady_) {
    warmCodec();
    if (!codecReady_) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  for (;;) {
    serviceRxDiagnostics();
    serviceSpeakerVolume();
#if REFLECTORMIC_DIAGNOSTICS
    if (codecTestState_ == 1 && !txRequested_ && !rxStreamActive_ && !playbackBusy()) {
      runCodecTest();
      continue;
    }
#endif
    if (captureBusy_) {
      captureTransmitSession();
      continue;
    }
    if (toneRequested_) startSpeakerTestTone();
    RxItem item;
    if (rxQueue_ && xQueueReceive(rxQueue_, &item, pdMS_TO_TICKS(2)) == pdTRUE)
      processReceiveItem(item);
    if (rxEndRequested_ && rxQueue_ && uxQueueMessagesWaiting(rxQueue_) == 0)
      finishReceiveStream();
    serviceSpeakerShutdown();
    // Even a backlogged worker must give Wi-Fi and the idle task CPU time.
    vTaskDelay(1);
  }
}

void AudioEngine::serviceSpeakerVolume() {
  if (!speakerMode_) return;
  if (hardware_.setSpeakerVolume(speakerVolume_)) return;
  ++playbackFailures_;
  hardware_.stopSpeaker();  // Muted; next queued audio may retry a full start.
  speakerMode_ = false;
}

void AudioEngine::warmCodec() {
  // Lazy codec tables must be ready before accepting PTT, even without the
  // optional benchmark. This silent frame never enters a network/audio queue.
  int16_t pcm[DSTAR_PCM_SAMPLES]{};
  uint8_t ambe[DSTAR_AMBE_BYTES]{};
  const bool ok = codec_.encode(pcm, ambe) && codec_.decode(ambe, pcm);
  codec_.resetEncoder();
  codec_.resetDecoder();
  codecWarmupFailed_ = !ok;
  codecReady_ = ok;
}

void AudioEngine::processReceiveItem(const RxItem& item) {
  if (item.type == RxItemType::Start) {
    speakerShutdownPending_ = false;
    speakerIdleSinceMs_ = 0;
    flushPlaybackChunk(true);
    codec_.resetDecoder();
#if REFLECTORMIC_DIAGNOSTICS
    ++rxTraceStream_;
    rxTraceFrame_ = 0;
#endif
    playbackFrames_ = 0;
    return;
  }
  if (!txRequested_ && awaitPlaybackSlot()) decodeReceivedFrame(item);
}

bool AudioEngine::awaitPlaybackSlot() {
  if (playbackFrames_ != 0) return !captureBusy_;
  // The speaker keeps PCM pointers. Wait BEFORE reusing a destination buffer.
  // Recovery can interrupt this wait if the speaker has stalled.
  while (M5.Speaker.isPlaying(0) >= 2 && !captureBusy_ && !rxRecoveryRequested_)
    vTaskDelay(1);
  return !captureBusy_ && !rxRecoveryRequested_;
}

void AudioEngine::decodeReceivedFrame(const RxItem& item) {
  int16_t* dest = playback_[playbackBuffer_] + playbackFrames_ * DSTAR_PCM_SAMPLES;
#if REFLECTORMIC_DIAGNOSTICS
  const uint32_t startedUs = micros();
  mbe_process_result result{};
  const bool decoded = codec_.decode(item.ambe, dest, &result);
  recordReceivedFrame(item, dest, decoded, micros() - startedUs, result);
#else
  const bool decoded = codec_.decode(item.ambe, dest);
#endif
  if (!decoded) { ++decodeFailures_; return; }
  ++decodedFrames_;
  if (++playbackFrames_ == PLAYBACK_CHUNK_FRAMES) flushPlaybackChunk(false);
}

void AudioEngine::flushPlaybackChunk(bool pad) {
  if (playbackFrames_ == 0) return;
  const size_t used = playbackFrames_ * DSTAR_PCM_SAMPLES;
  if (pad && used < DSTAR_PCM_SAMPLES * PLAYBACK_CHUNK_FRAMES) {
    memset(playback_[playbackBuffer_] + used, 0,
           (DSTAR_PCM_SAMPLES * PLAYBACK_CHUNK_FRAMES - used) * sizeof(int16_t));
  }
  if (!switchToSpeaker()) {
    playbackFailures_++;
  } else {
    RuntimeDiagnostics::mark(RuntimeStage::QueueingPlayback);
    // PTT/recovery may have arrived during the muted cold-start barrier.
    if (captureBusy_ || rxRecoveryRequested_) {
      hardware_.idleSpeaker();
    } else if (!M5.Speaker.playRaw(playback_[playbackBuffer_],
                            pad ? DSTAR_PCM_SAMPLES * PLAYBACK_CHUNK_FRAMES : used,
                            SAMPLE_RATE, false, 1, 0, false)) {
      playbackFailures_++;
    } else {
      hardware_.enableSpeakerOutput();
      playbackChunks_++;
    }
    RuntimeDiagnostics::mark(RuntimeStage::SpeakerActive);
  }
  playbackBuffer_ = (playbackBuffer_ + 1) % 3;
  playbackFrames_ = 0;
}

void AudioEngine::finishReceiveStream() {
  rxEndRequested_ = false;
  flushPlaybackChunk(true);
  speakerShutdownPending_ = true;
  speakerIdleSinceMs_ = 0;
}

void AudioEngine::serviceSpeakerShutdown() {
  if (!speakerShutdownPending_ || M5.Speaker.isPlaying()) {
    speakerIdleSinceMs_ = 0;
    return;
  }
  // isPlaying() observes the software mixer, not the I2S DMA tail. Allow one
  // complete DMA ring plus a scheduling guard before muting the amplifier.
  if (speakerMode_) {
    const uint32_t now = millis();
    if (!speakerIdleSinceMs_) speakerIdleSinceMs_ = now;
    const auto cfg = M5.Speaker.config();
    const uint32_t drainMs =
        (cfg.dma_buf_count * cfg.dma_buf_len * 1000U + cfg.sample_rate - 1) /
        cfg.sample_rate + 20;
    if (now - speakerIdleSinceMs_ < drainMs) return;
  }
  RuntimeDiagnostics::mark(RuntimeStage::StoppingAudio);
  hardware_.idleSpeaker();
  // Keep speakerMode_: the driver remains ready on silent DMA. Full teardown
  // is reserved for PTT and explicit receive recovery, not each RX ending.
  speakerShutdownPending_ = false;
  RuntimeDiagnostics::mark(RuntimeStage::Ready);
  Serial.println("[audio] speaker idle; clocks retained, amplifier off");
}

bool AudioEngine::requestRxRecovery() {
  if (!task_ || codecTestBusy() || txRequested_ || captureBusy_ || toneRequested_)
    return false;
  // Capture the evidence before resetting state. HTTP never touches I2S or
  // codec prediction state directly; the worker services these in order.
#if REFLECTORMIC_DIAGNOSTICS
  rxTrace_.request();
#endif
  rxRecoveryRequested_ = true;
  return true;
}

void AudioEngine::serviceRxDiagnostics() {
#if REFLECTORMIC_DIAGNOSTICS
  rxTrace_.service(rxRecoveries_.load());
#endif
  if (!rxRecoveryRequested_.exchange(false)) return;
  if (txRequested_ || captureBusy_) return;  // PTT won the race; leave TX alone.
  hardware_.stopSpeaker();
  speakerMode_ = false;
  speakerShutdownPending_ = false;
  playbackFrames_ = 0;
  codec_.resetDecoder();
#if REFLECTORMIC_DIAGNOSTICS
  ++rxTraceStream_;
  rxTraceFrame_ = 0;
#endif
  ++rxRecoveries_;
  // Keep the reflector link and FIFO. The next received frame restarts output.
}

