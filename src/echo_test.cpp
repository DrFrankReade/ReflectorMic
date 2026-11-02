#include "echo_test.h"

#include <cstring>
#include <esp_heap_caps.h>

bool EchoTest::begin(AudioEngine &audio) {
  audio_ = &audio;
  frames_ = static_cast<uint8_t *>(heap_caps_malloc(
      MAX_FRAMES * DSTAR_AMBE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frames_) {
    state_ = EchoTestState::Unavailable;
    status_ = "unable to reserve PSRAM for echo test";
    return false;
  }
  state_ = EchoTestState::Idle;
  status_ = "ready";
  return true;
}

void EchoTest::setUnavailable(const String &reason) {
  state_ = EchoTestState::Unavailable;
  status_ = reason;
}

bool EchoTest::arm() {
  if (!audio_ || !frames_ || state_ != EchoTestState::Idle) return false;
  if (audio_->captureBusy() || audio_->playbackBusy() || audio_->codecTestBusy()) {
    status_ = "audio path is busy; wait for playback or the timing test to finish";
    return false;
  }
  frameCount_ = 0;
  playbackIndex_ = 0;
  state_ = EchoTestState::Armed;
  status_ = "armed; hold PTT to record up to 10 seconds";
  return true;
}

bool EchoTest::cancel() {
  if (state_ != EchoTestState::Armed) return false;
  state_ = EchoTestState::Idle;
  status_ = "cancelled";
  return true;
}

bool EchoTest::startRecording() {
  if (state_ != EchoTestState::Armed) return false;
  if (!audio_->startTransmit(true)) {
    status_ = "PTT detected; microphone start failed (" +
              audio_->captureDiagnostic() + ")";
    return false;
  }
  frameCount_ = 0;
  recordingStartedMs_ = millis();
  state_ = EchoTestState::Recording;
  status_ = "recording";
  return true;
}

void EchoTest::drainEncodedFrames() {
  uint8_t frame[DSTAR_AMBE_BYTES];
  while (frameCount_ < MAX_FRAMES && audio_->popEncodedFrame(frame)) {
    memcpy(frames_ + frameCount_ * DSTAR_AMBE_BYTES, frame,
           DSTAR_AMBE_BYTES);
    frameCount_++;
  }
}

void EchoTest::stopRecording(const char *reason) {
  if (state_ != EchoTestState::Recording) return;
  drainEncodedFrames();
  audio_->stopTransmit();
  finalizingStartedMs_ = millis();
  state_ = EchoTestState::Finalizing;
  status_ = String("finishing capture: ") + reason;
}

void EchoTest::beginPlayback() {
  playbackIndex_ = 0;
  nextPlaybackMs_ = millis();
  playbackDoneMs_ = 0;
  playbackStarted_ = false;
  playbackEndQueued_ = false;
  state_ = EchoTestState::Playing;
  status_ = "playing recorded AMBE audio";
}

void EchoTest::service() {
  switch (state_) {
    case EchoTestState::Recording: serviceRecording(); break;
    case EchoTestState::Finalizing: serviceFinalizing(); break;
    case EchoTestState::Playing: servicePlaying(); break;
    default: break;
  }
}

void EchoTest::serviceRecording() {
  drainEncodedFrames();
  if (frameCount_ >= MAX_FRAMES ||
      millis() - recordingStartedMs_ >= MAX_DURATION_MS) {
    stopRecording("10-second limit");
  } else if (!audio_->transmitting()) {
    stopRecording("capture stopped");
  }
}

void EchoTest::serviceFinalizing() {
  drainEncodedFrames();
  const uint32_t elapsed = millis() - finalizingStartedMs_;
  // R13 may encode slower than capture. Preserve the entire ten-second PCM
  // recording in PSRAM and finish its backlog instead of truncating at 3 s.
  if (audio_->captureBusy()) {
    status_ = "encoding captured audio: " + String(frameCount_) + "/" +
              String(audio_->capturedPcmFrames()) + " frames; " +
              String(audio_->pendingPcmFrames()) + " pending";
    if (elapsed < 180000) return;
    state_ = EchoTestState::Idle;
    status_ = "audio capture task did not settle (" +
              audio_->captureDiagnostic() + ")";
    return;
  }
  // Completion can arrive after the first drain on this core. Collect the
  // final AMBE frame before changing state and releasing the TX queue.
  drainEncodedFrames();
  Serial.printf("[echo] captured %u AMBE frames; %s\n", frameCount_,
                audio_->captureDiagnostic().c_str());
  if (frameCount_ == 0) {
    if (elapsed < 1500) return;
    state_ = EchoTestState::Idle;
    status_ = "no audio captured (" + audio_->captureDiagnostic() + ")";
    return;
  }
  beginPlayback();
}

void EchoTest::servicePlaying() {
  if (!playbackStarted_) {
    audio_->beginReceiveStream();
    playbackStarted_ = true;
  }

  const uint32_t now = millis();
  queuePlaybackFrames(now);

  if (playbackIndex_ == frameCount_ && !playbackEndQueued_ &&
      audio_->endReceiveStream()) {
    playbackEndQueued_ = true;
    // Use the real decoder/speaker drain state instead of guessing a delay.
    playbackDoneMs_ = now + 5000;
  }
  if (playbackEndQueued_ && !audio_->playbackBusy()) {
    state_ = EchoTestState::Idle;
    status_ = "complete; recorded " + String(frameCount_) + " AMBE frames";
  } else if (playbackEndQueued_ &&
             static_cast<int32_t>(now - playbackDoneMs_) >= 0) {
    state_ = EchoTestState::Idle;
    status_ = "playback drain timeout (" + audio_->playbackDiagnostic() + ")";
  }
}

void EchoTest::queuePlaybackFrames(uint32_t now) {
  uint8_t sent = 0;
  while (playbackIndex_ < frameCount_ &&
         static_cast<int32_t>(now - nextPlaybackMs_) >= 0 && sent++ < 4) {
    const uint8_t *frame = frames_ + playbackIndex_ * DSTAR_AMBE_BYTES;
    if (!audio_->submitReceivedFrame(frame)) break;
    playbackIndex_++;
    nextPlaybackMs_ += 20;
  }
}

String EchoTest::stateText() const {
  switch (state_) {
    case EchoTestState::Unavailable: return "unavailable";
    case EchoTestState::Idle: return "idle";
    case EchoTestState::Armed: return "armed";
    case EchoTestState::Recording: return "recording";
    case EchoTestState::Finalizing: return "finalizing";
    case EchoTestState::Playing: return "playing";
  }
  return "unknown";
}
