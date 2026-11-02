#pragma once

#include <Arduino.h>

#include "audio_engine.h"

enum class EchoTestState : uint8_t {
  Unavailable,
  Idle,
  Armed,
  Recording,
  Finalizing,
  Playing,
};

// Stores up to 4,500 bytes of AMBE in PSRAM. AudioEngine also stages the bounded
// PCM recording there so a slow encoder cannot leave gaps in the microphone
// input. Playback waits for encoding, then uses the normal 20 ms receive path.
class EchoTest {
 public:
  bool begin(AudioEngine &audio);
  void setUnavailable(const String &reason);
  bool arm();
  bool cancel();
  bool startRecording();
  void stopRecording(const char *reason);
  void service();

  EchoTestState state() const { return state_; }
  bool active() const { return state_ != EchoTestState::Idle &&
                               state_ != EchoTestState::Unavailable; }
  bool armed() const { return state_ == EchoTestState::Armed; }
  bool recording() const { return state_ == EchoTestState::Recording; }
  bool playing() const { return state_ == EchoTestState::Playing; }
  String stateText() const;
  String statusText() const { return status_; }
  uint16_t frameCount() const { return frameCount_; }

  static constexpr uint16_t MAX_FRAMES = 10U * 50U;
  static constexpr uint32_t MAX_DURATION_MS = 10000;

 private:
  void serviceRecording();
  void serviceFinalizing();
  void servicePlaying();
  void queuePlaybackFrames(uint32_t now);
  void drainEncodedFrames();
  void beginPlayback();

  AudioEngine *audio_ = nullptr;
  uint8_t *frames_ = nullptr;
  EchoTestState state_ = EchoTestState::Unavailable;
  String status_ = "PSRAM unavailable";
  uint16_t frameCount_ = 0;
  uint16_t playbackIndex_ = 0;
  uint32_t recordingStartedMs_ = 0;
  uint32_t finalizingStartedMs_ = 0;
  uint32_t nextPlaybackMs_ = 0;
  uint32_t playbackDoneMs_ = 0;
  bool playbackStarted_ = false;
  bool playbackEndQueued_ = false;
};
