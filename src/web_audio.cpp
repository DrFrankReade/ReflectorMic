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
