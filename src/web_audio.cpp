#include "web_server.h"

#if REFLECTORMIC_DIAGNOSTICS
#include "encoder_profile.h"
#endif

void ReflectorMicWebServer::armEcho() {
  if (reflector_->transmitting() || reflector_->incoming() || audio_->transmitting()) {
    server_.send(409, "application/json", "{\"error\":\"audio path is busy\"}");
    return;
  }
  const bool ok = echo_->arm();
  server_.send(ok ? 200 : 409, "application/json",
               String("{\"ok\":") + (ok ? "true" : "false") +
                   ",\"status\":" + json(echo_->statusText()) + "}");
}

void ReflectorMicWebServer::cancelEcho() {
  const bool ok = echo_->cancel();
  server_.send(ok ? 200 : 409, "application/json",
               String("{\"ok\":") + (ok ? "true" : "false") +
                   ",\"status\":" + json(echo_->statusText()) + "}");
}

void ReflectorMicWebServer::testSpeaker() {
  if (reflector_->transmitting() || reflector_->incoming() ||
      audio_->transmitting() || echo_->active()) {
    server_.send(409, "application/json", "{\"error\":\"audio path is busy\"}");
    return;
  }
  const bool ok = audio_->playSpeakerTestTone();
  server_.send(ok ? 200 : 500, "application/json",
               ok ? "{\"ok\":true}" :
                    "{\"error\":\"speaker test could not start\"}");
}

void ReflectorMicWebServer::recoverRx() {
  const bool ok = !echo_->active() && !reflector_->transmitting() &&
                  audio_->requestRxRecovery();
  server_.send(ok ? 202 : 409, "application/json", ok ? "{\"ok\":true}" :
      "{\"error\":\"Audio path is busy\"}");
}

#if REFLECTORMIC_DIAGNOSTICS
void ReflectorMicWebServer::testCodec() {
  const bool ok = !reflector_->transmitting() && !reflector_->incoming() &&
                  !echo_->active() && audio_->requestCodecTest();
  server_.send(ok ? 202 : 409, "application/json",
               ok ? "{\"ok\":true}" : "{\"error\":\"audio path is busy\"}");
}

void ReflectorMicWebServer::sendAudioProfile() {
  server_.send(200, "application/json", encoderProfileJson());
}

void ReflectorMicWebServer::captureRxSnapshot() {
  const bool ok = !echo_->active() && !audio_->captureBusy() &&
                  audio_->requestRxSnapshot();
  server_.send(ok ? 202 : 409, "application/json", ok ? "{\"ok\":true}" :
      "{\"error\":\"RX diagnostic unavailable or busy\"}");
}

void ReflectorMicWebServer::sendRxTrace() {
  const auto* snapshot = audio_->rxSnapshot();
  if (!snapshot) {
    server_.send(409, "application/json", "{\"error\":\"Snapshot not ready\"}");
    return;
  }
  const size_t bytes = 32 + snapshot->count * sizeof(RxTraceFrame);
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("Content-Disposition", "attachment; filename=ReflectorMic-RX.bin");
  server_.setContentLength(bytes);
  server_.send(200, "application/octet-stream", "");
  server_.sendContent(reinterpret_cast<const char*>(snapshot), bytes);
}
#endif

void ReflectorMicWebServer::setSpeakerFilters() {
  // Reject malformed or wrapped integers before publishing any audio state.
  auto frequency = [this](const char* key) -> uint16_t {
    const String text = server_.arg(key);
    if (text.isEmpty() || text.length() > 4) return 0;
    for (size_t i = 0; i < text.length(); ++i)
      if (text[i] < '0' || text[i] > '9') return 0;
    return static_cast<uint16_t>(text.toInt());
  };
  const String enabled = server_.arg("enabled");
  const String slope = server_.hasArg("slopeDb") ? server_.arg("slopeDb") : String("6");
  const SpeakerFilterSettings filters{enabled == "1", frequency("lowCutHz"),
                                     frequency("highCutHz"), uint8_t(slope == "12" ? 12 : 6)};
  if ((enabled != "0" && enabled != "1") ||
      (slope != "6" && slope != "12") || !filters.valid()) {
    server_.send(400, "application/json", "{\"error\":\"Invalid speaker filter settings\"}");
    return;
  }
  audio_->setSpeakerFilters(filters); // No flash, I2C, queue reset, or codec work.
  if (server_.arg("save") == "1") {
    if (pttPressed_ || reflector_->incoming() || reflector_->transmitting() ||
        audio_->playbackBusy() || audio_->captureBusy() || echo_->active()) {
      server_.send(409, "application/json", "{\"error\":\"Preview is active. Save when audio is idle.\"}");
      return;
    }
    if (!store_->saveSpeakerFilters(filters)) {
      server_.send(500, "application/json", "{\"error\":\"Preview is active, but settings could not be saved.\"}");
      return;
    }
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

void ReflectorMicWebServer::setSpeakerVolume() {
  const String text = server_.arg("volume");
  bool valid = !text.isEmpty() && text.length() <= 3;
  for (size_t i = 0; i < text.length(); ++i)
    valid &= text[i] >= '0' && text[i] <= '9';
  const long volume = valid ? text.toInt() : -1;
  if (volume < 8 || volume > 255) {
    server_.send(400, "application/json", "{\"error\":\"Speaker volume must be between 8 and 255\"}");
    return;
  }
  // The audio worker applies this through the DAC's verified, smoothed gain
  // control. HTTP never writes flash or touches the active I2S/mixer state.
  audio_->setSpeakerVolume(static_cast<uint8_t>(volume));
  server_.send(200, "application/json", "{\"ok\":true}");
}
