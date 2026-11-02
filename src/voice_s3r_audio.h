#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <atomic>

// Atom VoiceS3R-specific ES8311 and NS4150B lifecycle. ReflectorMic is a
// fixed-hardware, displayless product, so this wrapper supplies the documented
// pins and codec sequences instead of relying on M5Unified board autodetection.
class VoiceS3rAudioHardware {
 public:
  bool begin();
  bool startMicrophone();
  bool startSpeaker(uint8_t volume);
  // Called only by the audio worker. HTTP publishes the requested level.
  bool setSpeakerVolume(uint8_t volume);
  void stopMicrophone();
  void stopSpeaker();
  // After the DMA tail drains, mute without cycling shared I2S clocks.
  void idleSpeaker();
  void enableSpeakerOutput();
  void stopAll();

  bool codecPresent() const { return codecPresent_; }
  bool amplifierEnabled() const { return amplifierEnabled_; }
  String diagnostic() const;

 private:
  bool configureAudioClasses();
  bool writeRegister(uint8_t reg, uint8_t value);
  bool configureMicrophoneCodec(bool enabled);
  bool configureSpeakerCodec();
  bool primeSpeaker();
  bool writeVerifiedRegister(uint8_t reg, uint8_t value);
  bool initializeShutdownGuard();
  void endSpeakerDriver();
  static void shutdownTimedOut(void* arg);
  void setAmplifier(bool enabled);
  void fail(const String &message);

  bool codecPresent_ = false;
  bool microphoneRunning_ = false;
  bool speakerRunning_ = false;
  bool amplifierEnabled_ = false;
  bool speakerOutputRequested_ = false;
  uint8_t speakerVolume_ = 0;
  std::atomic<int> speakerGainRegister_{-1};
  esp_timer_handle_t shutdownTimer_ = nullptr;
  portMUX_TYPE shutdownMux_ = portMUX_INITIALIZER_UNLOCKED;
  bool shutdownPending_ = false;  // Protected by shutdownMux_, including timer.
  int64_t shutdownDeadlineUs_ = 0;
  String lastError_ = "not initialized";
};
