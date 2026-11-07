#include "voice_s3r_audio.h"
#include "runtime_diagnostics.h"
#include "speaker_gain.h"

#include <driver/i2c.h>
#include <driver/i2s.h>

namespace {

constexpr uint8_t ES8311_ADDRESS = 0x18;
constexpr uint32_t ES8311_I2C_FREQUENCY = 100000;
constexpr int I2C_SDA_PIN = 45;
constexpr int I2C_SCL_PIN = 0;
constexpr int I2S_SPEAKER_DATA_PIN = 48;
constexpr int I2S_MICROPHONE_DATA_PIN = 4;
constexpr int I2S_WORD_SELECT_PIN = 3;
constexpr int I2S_BIT_CLOCK_PIN = 17;
constexpr int I2S_MASTER_CLOCK_PIN = 11;
constexpr int AMPLIFIER_ENABLE_PIN = 18;
constexpr uint32_t DSTAR_SAMPLE_RATE = 8000;
constexpr uint8_t MICROPHONE_OVERSAMPLING = 2;
constexpr uint32_t MICROPHONE_I2S_RATE =
    DSTAR_SAMPLE_RATE * MICROPHONE_OVERSAMPLING;
constexpr uint32_t SPEAKER_I2S_RATE = 48000;
constexpr uint32_t SPEAKER_START_TIMEOUT_MS = 250;
constexpr uint32_t SPEAKER_STOP_TIMEOUT_MS = 1000;
// Static lifetime: M5Unified retains the pointer until the mixer consumes it.
const int16_t SPEAKER_START_SILENCE[256] = {};

}  // namespace

bool VoiceS3rAudioHardware::begin() {
  // Do not call M5.begin() for this headless product. M5Unified currently can
  // classify some VoiceS3R units as AtomS3R-Ext (board 143), leaving both
  // audio classes disabled. Initialize only the internal bus we actually use.
  if (!M5.In_I2C.begin(I2C_NUM_1, I2C_SDA_PIN, I2C_SCL_PIN)) {
    fail("VoiceS3R internal I2C initialization failed");
    return false;
  }

  codecPresent_ = M5.In_I2C.scanID(ES8311_ADDRESS, ES8311_I2C_FREQUENCY);
  if (!codecPresent_) {
    fail("ES8311 codec not found at I2C address 0x18");
    return false;
  }

  if (!configureAudioClasses() || !initializeShutdownGuard()) return false;
  setAmplifier(false);
  // I2S tasks remain stopped until PTT, reflector RX, or a speaker test needs
  // one direction. This keeps boot passive and recovery-safe.
  lastError_ = "ES8311 ready; explicit Atom VoiceS3R profile";
  return true;
}

bool VoiceS3rAudioHardware::configureAudioClasses() {
  auto mic = M5.Mic.config();
  mic.pin_mck = I2S_MASTER_CLOCK_PIN;
  mic.pin_bck = I2S_BIT_CLOCK_PIN;
  mic.pin_ws = I2S_WORD_SELECT_PIN;
  mic.pin_data_in = I2S_MICROPHONE_DATA_PIN;
  mic.i2s_port = I2S_NUM_1;
  mic.input_channel = m5::input_stereo;
  mic.magnification = 1;
  // MBELIB-neo consumes 160 samples at 8 kHz. M5Unified's oversampling path
  // obtains two physical samples for every delivered sample, keeping the
  // ES8311/I2S input at the board's supported 16 kHz clock.
  mic.sample_rate = DSTAR_SAMPLE_RATE;
  mic.over_sampling = MICROPHONE_OVERSAMPLING;
  M5.Mic.config(mic);

  auto speaker = M5.Speaker.config();
  speaker.pin_mck = I2S_MASTER_CLOCK_PIN;
  speaker.pin_bck = I2S_BIT_CLOCK_PIN;
  speaker.pin_ws = I2S_WORD_SELECT_PIN;
  speaker.pin_data_out = I2S_SPEAKER_DATA_PIN;
  speaker.i2s_port = I2S_NUM_1;
  // Two explicit 16-bit slots avoid the S3's mono FIFO/channel duplication
  // workaround. M5 duplicates our mono PCM into L/R; ES8311 consumes left.
  speaker.stereo = true;
  // At master/channel 255, magnification 8 gives 0.9845x PCM, below unity.
  // The old 1x mixer followed by +32 dB in the DAC could clip normal speech.
  speaker.magnification = 8;
  speaker.task_pinned_core = 1;
  speaker.task_priority = 3;
  // speaker_build.py retains M5 resampling but leaves clock programming to
  // IDF: WS=48 kHz, BCLK=32*fs, MCLK=256*fs on GPIO11.
  speaker.sample_rate = SPEAKER_I2S_RATE;
  M5.Speaker.config(speaker);

  pinMode(AMPLIFIER_ENABLE_PIN, OUTPUT);
  setAmplifier(false);
  return true;
}

bool VoiceS3rAudioHardware::startMicrophone() {
  stopSpeaker();
  if (!configureMicrophoneCodec(true)) return false;
  if (!M5.Mic.begin()) {
    fail("microphone I2S start failed");
    return false;
  }
  microphoneRunning_ = true;
  return true;
}

bool VoiceS3rAudioHardware::startSpeaker(uint8_t volume) {
  if (speakerRunning_) return setSpeakerVolume(volume);
  setAmplifier(false);
  speakerOutputRequested_ = false;
  speakerGainRegister_ = -1;
  stopMicrophone();
  if (!M5.Speaker.begin()) {
    endSpeakerDriver();  // Also clean up a partly installed driver.
    fail("speaker I2S start failed");
    return false;
  }
  speakerRunning_ = true;
  // Consume silence through the real mixer/DMA path before enabling the DAC.
  // The IDF driver owns fixed MCLK/BCLK/WS; the mixer never rewrites them.
  // Only this worker waits; PTT/UI/network continue on their own tasks.
  if (!primeSpeaker() || !configureSpeakerCodec()) {
    stopSpeaker();
    return false;
  }
  // Keep full-resolution PCM on the wire; the ES8311 applies user attenuation
  // after receiving it. Restore this only once the hardware gain is verified.
  if (!setSpeakerVolume(volume)) {
    stopSpeaker();
    return false;
  }
  // ES8311 User Guide 11.2: 0.25 dB per four LRCK periods (~0.33 ms/dB
  // at 48 kHz) smooths slider changes entirely inside the DAC. Enable it
  // AFTER the initial gain is applied, so startup does not fade in from
  // -95.5 dB over the first speech samples. Bit 3 keeps the EQ bypassed.
  if (!writeVerifiedRegister(0x37, 0x18)) {
    fail("ES8311 speaker volume ramp verification failed");
    stopSpeaker();
    return false;
  }
  M5.Speaker.setVolume(255);
  // Allow the hardware DAC to settle on zero PCM before any audible data.
  vTaskDelay(pdMS_TO_TICKS(20));
  return true;
}

bool VoiceS3rAudioHardware::setSpeakerVolume(uint8_t volume) {
  if (!speakerRunning_) return false;
  if (speakerGainRegister_.load() >= 0 && speakerVolume_ == volume) return true;
  const uint8_t gain = speaker_gain::dacRegister(volume);
  // Zero is a real amplifier mute, even if the serial audio is misframed.
  if (!volume) setAmplifier(false);
  if (speakerGainRegister_.load() != gain) {
    if (!writeVerifiedRegister(0x32, gain)) {
      setAmplifier(false);
      speakerGainRegister_ = -1;
      fail("ES8311 speaker volume verification failed; amplifier muted");
      return false;
    }
    speakerGainRegister_ = gain;
  }
  speakerVolume_ = volume;
  if (speakerOutputRequested_) setAmplifier(volume != 0);
  return true;
}

bool VoiceS3rAudioHardware::primeSpeaker() {
  M5.Speaker.setVolume(0);
  if (!M5.Speaker.playRaw(SPEAKER_START_SILENCE, 256, SPEAKER_I2S_RATE,
                          false, 16, 0, false)) {
    fail("speaker silence queue failed");
    return false;
  }
  const uint32_t started = millis();
  while (M5.Speaker.isPlaying(0)) {
    if (millis() - started >= SPEAKER_START_TIMEOUT_MS) {
      fail("speaker clock startup timed out");
      return false;
    }
    vTaskDelay(1);
  }
  // Mixer completion is not DMA completion. Flush one entire ring plus guard.
  const auto cfg = M5.Speaker.config();
  const uint32_t drainMs =
      (cfg.dma_buf_count * cfg.dma_buf_len * 1000U + cfg.sample_rate - 1) /
      cfg.sample_rate + 20;
  vTaskDelay(pdMS_TO_TICKS(drainMs));
  return true;
}

void VoiceS3rAudioHardware::stopMicrophone() {
  if (!microphoneRunning_) return;
  M5.Mic.end();
  microphoneRunning_ = false;
  configureMicrophoneCodec(false);
}

void VoiceS3rAudioHardware::stopSpeaker() {
  // Mute BEFORE stopping clocks or dismantling DMA, including PTT/recovery.
  setAmplifier(false);
  speakerOutputRequested_ = false;
  speakerGainRegister_ = -1;
  if (speakerRunning_) {
    // end() discards queued PCM after joining the mixer. A preceding stop()
    // can block trying to publish into a stalled mixer's full queue.
    endSpeakerDriver();
  }
  speakerRunning_ = false;
}

bool VoiceS3rAudioHardware::initializeShutdownGuard() {
  if (shutdownTimer_) return true;
  esp_timer_create_args_t timer{};
  timer.callback = shutdownTimedOut;
  timer.arg = this;
  timer.dispatch_method = ESP_TIMER_TASK;
  timer.name = "speaker-stop";
  if (esp_timer_create(&timer, &shutdownTimer_) == ESP_OK) return true;
  fail("unable to allocate speaker shutdown guard");
  return false;
}

void VoiceS3rAudioHardware::endSpeakerDriver() {
  // M5 end() joins a mixer that can be blocked in a portMAX_DELAY DMA write.
  // Never uninstall that port beneath its live task. An independent timer
  // escalates a genuinely wedged join to a reboot, with the amp already off.
  portENTER_CRITICAL(&shutdownMux_);
  shutdownDeadlineUs_ = esp_timer_get_time() + SPEAKER_STOP_TIMEOUT_MS * 1000LL;
  shutdownPending_ = true;
  portEXIT_CRITICAL(&shutdownMux_);
  if (!shutdownTimer_ || esp_timer_start_once(
          shutdownTimer_, SPEAKER_STOP_TIMEOUT_MS * 1000ULL) != ESP_OK) {
    RuntimeDiagnostics::mark(RuntimeStage::SpeakerStopTimeout);
    ESP.restart();  // Fail closed rather than enter an unguarded join.
    return;
  }
  M5.Speaker.end();
  portENTER_CRITICAL(&shutdownMux_);
  shutdownPending_ = false;
  portEXIT_CRITICAL(&shutdownMux_);
  esp_timer_stop(shutdownTimer_);
}

void VoiceS3rAudioHardware::shutdownTimedOut(void* arg) {
  auto& hardware = *static_cast<VoiceS3rAudioHardware*>(arg);
  // A callback already dispatched when stop() ran must not affect a newer
  // shutdown. Test deadline and disarm atomically with respect to rearming.
  portENTER_CRITICAL(&hardware.shutdownMux_);
  const bool expired = hardware.shutdownPending_ &&
                      esp_timer_get_time() >= hardware.shutdownDeadlineUs_;
  if (expired) hardware.shutdownPending_ = false;
  portEXIT_CRITICAL(&hardware.shutdownMux_);
  if (!expired) return;
  RuntimeDiagnostics::mark(RuntimeStage::SpeakerStopTimeout);
  ESP.restart();
}

void VoiceS3rAudioHardware::idleSpeaker() {
  // The pinned M5 external-DAC path clears DMA to silence and leaves I2S
  // running while its task sleeps. Keep that clock state between stations.
  // PTT and explicit recovery still call stopSpeaker() to release the port.
  setAmplifier(false);
  speakerOutputRequested_ = false;
}

void VoiceS3rAudioHardware::enableSpeakerOutput() {
  if (!speakerRunning_ || speakerGainRegister_.load() < 0) return;
  speakerOutputRequested_ = true;
  setAmplifier(speakerVolume_ != 0);
}

void VoiceS3rAudioHardware::stopAll() {
  stopMicrophone();
  stopSpeaker();
}

bool VoiceS3rAudioHardware::configureMicrophoneCodec(bool enabled) {
  // M5Stack's VoiceS3R ES8311 callback sequence. The disabled sequence powers
  // down the ADC path between PTT sessions.
  static constexpr uint8_t enabledRegisters[][2] = {
      {0x00, 0x80}, {0x01, 0xBA}, {0x02, 0x18}, {0x0D, 0x01},
      {0x0E, 0x02}, {0x14, 0x10}, {0x17, 0xFF}, {0x1C, 0x6A},
  };
  static constexpr uint8_t disabledRegisters[][2] = {
      {0x0D, 0xFC}, {0x0E, 0x6A}, {0x00, 0x00},
  };
  const uint8_t (*registers)[2] = enabled ? enabledRegisters : disabledRegisters;
  const size_t count = enabled ? sizeof(enabledRegisters) / sizeof(enabledRegisters[0])
                               : sizeof(disabledRegisters) / sizeof(disabledRegisters[0]);
  for (size_t i = 0; i < count; ++i) {
    if (!writeRegister(registers[i][0], registers[i][1])) {
      fail(enabled ? "unable to enable ES8311 microphone path"
                   : "unable to power down ES8311 microphone path");
      return false;
    }
  }
  return true;
}

bool VoiceS3rAudioHardware::configureSpeakerCodec() {
  // ES8311 User Guide section 9.1: assert digital/clock resets with CSM off,
  // wait several milliseconds, then release with CSM on. Merely writing 0x80
  // (the old path) never cleared a bad clock/DAC state after an I2S transition.
  // The I2C control registers are outside this reset domain.
  if (!writeVerifiedRegister(0x00, 0x1F)) {
    fail("unable to reset ES8311 playback state");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(5));
  // Explicit profile: dedicated 256*fs MCLK, single-speed DAC, 16-bit Philips
  // I2S left slot, no ADC loopback, DRC disabled, EQ bypass. Reset does NOT
  // restore I2C registers, so restore all relevant clock and DAC controls.
  // 0x32=0xBF is 0 dB; 0xFF (the former setting) is +32 dB, NOT unity.
  // Start at minimum gain, then apply the slider while the amplifier is off.
  // Restore normal analog quality (0x0F=0), not the reduced-bias low-power
  // modes. Keep the volume ramp off until the initial gain has been set.
  static constexpr uint8_t registers[][2] = {
      {0x01, 0x35}, {0x02, 0x00}, {0x03, 0x10}, {0x04, 0x10},
      {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF},
      {0x09, 0x0C}, {0x31, 0x00}, {0x32, 0x00}, {0x34, 0x00},
      {0x37, 0x08}, {0x44, 0x00}, {0x0F, 0x00}, {0x12, 0x00}, {0x13, 0x10},
      {0x0D, 0x01}, {0x00, 0x80},
  };
  for (const auto &item : registers) {
    if (!writeVerifiedRegister(item[0], item[1])) {
      fail("ES8311 speaker register verification failed");
      return false;
    }
  }
  return true;
}

bool VoiceS3rAudioHardware::writeVerifiedRegister(uint8_t reg, uint8_t value) {
  // An acknowledged write alone does not establish the chip's final state.
  // Retry transient bus failures, but never enable the amp on a mismatch.
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    uint8_t actual = 0;
    if (writeRegister(reg, value) &&
        M5.In_I2C.readRegister(ES8311_ADDRESS, reg, &actual, 1,
                              ES8311_I2C_FREQUENCY) && actual == value)
      return true;
    vTaskDelay(1);
  }
  return false;
}

bool VoiceS3rAudioHardware::writeRegister(uint8_t reg, uint8_t value) {
  return M5.In_I2C.writeRegister8(ES8311_ADDRESS, reg, value,
                                  ES8311_I2C_FREQUENCY);
}

void VoiceS3rAudioHardware::setAmplifier(bool enabled) {
  digitalWrite(AMPLIFIER_ENABLE_PIN, enabled ? HIGH : LOW);
  amplifierEnabled_ = enabled;
}

void VoiceS3rAudioHardware::fail(const String &message) {
  lastError_ = message;
  Serial.printf("[audio-hw] %s\n", message.c_str());
}

String VoiceS3rAudioHardware::diagnostic() const {
  String out = lastError_ + "; profile=Atom VoiceS3R (explicit)";
  out += "; M5 autodetect=bypassed";
  const auto mic = M5.Mic.config();
  const auto speaker = M5.Speaker.config();
  out += "; mic=" + String(mic.pin_data_in) + '/' + String(mic.pin_bck) +
         '/' + String(mic.pin_ws) + '/' + String(mic.pin_mck);
  out += "; spk=" + String(speaker.pin_data_out) + '/' +
          String(speaker.pin_bck) + '/' + String(speaker.pin_ws);
  out += "; mic PCM=" + String(DSTAR_SAMPLE_RATE) + " Hz x" +
         String(mic.over_sampling) + " (I2S " +
         String(MICROPHONE_I2S_RATE) + " Hz)";
  out += "; speaker I2S=" + String(speaker.sample_rate) + " Hz";
  out += "; speaker clock=IDF stereo16/MCLK256; speaker task=core1/prio3";
  const int gain = speakerGainRegister_.load();
  if (gain >= 0) out += "; DAC gain=" + String((gain - speaker_gain::UNITY) * 0.5f) + " dB";
  out += "; PSRAM=" + String(ESP.getPsramSize() / 1024U) + " KB";
  out += amplifierEnabled_ ? "; amp=on" : "; amp=off";
  return out;
}
