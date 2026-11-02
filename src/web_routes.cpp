#include "web_server.h"

#include "web_ui.h"

void ReflectorMicWebServer::routeRequests() {
  for (size_t i = 0; i < reflectormic_web::kAssetCount; ++i) {
    const auto *asset = &reflectormic_web::kAssets[i];
    server_.on(asset->path, HTTP_GET, [this, asset]() {
      server_.sendHeader("Cache-Control", "no-cache");
      server_.send_P(200, asset->mime, asset->data, asset->size);
    });
  }
  server_.on("/api/config", HTTP_GET, [this]() { sendConfig(); });
  server_.on("/api/config", HTTP_POST, [this]() { saveConfig(); });
  server_.on("/api/status", HTTP_GET, [this]() { sendStatus(); });
  server_.on("/api/wifi/scan", HTTP_GET, [this]() { scanWifi(); });
  routeDirectoryRequests();
  routeAudioRequests();
  server_.on("/api/link", HTTP_POST, [this]() { linkReflector(); });
  server_.on("/api/unlink", HTTP_POST, [this]() { unlinkReflector(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { requestReboot(); });
  routeCaptiveRequests();
}

void ReflectorMicWebServer::routeAudioRequests() {
  server_.on("/api/echo/arm", HTTP_POST, [this]() { armEcho(); });
  server_.on("/api/echo/cancel", HTTP_POST, [this]() { cancelEcho(); });
  server_.on("/api/audio/speaker-test", HTTP_POST, [this]() { testSpeaker(); });
#if REFLECTORMIC_DIAGNOSTICS
  server_.on("/api/audio/codec-test", HTTP_POST, [this]() { testCodec(); });
  server_.on("/api/audio/profile", HTTP_GET, [this]() { sendAudioProfile(); });
  server_.on("/api/audio/rx-snapshot", HTTP_POST, [this]() { captureRxSnapshot(); });
  server_.on("/api/audio/rx-trace", HTTP_GET, [this]() { sendRxTrace(); });
#endif
  server_.on("/api/audio/rx-recover", HTTP_POST, [this]() { recoverRx(); });
}

void ReflectorMicWebServer::routeDirectoryRequests() {
  server_.on("/api/reflectors", HTTP_GET, [this]() { listReflectors(); });
  server_.on("/api/directory", HTTP_GET, [this]() { sendDirectoryStatus(); });
  server_.on("/api/directory/upload", HTTP_POST,
             [this]() { finishDirectoryUpload(); },
             [this]() { handleDirectoryUpload(); });
}

void ReflectorMicWebServer::routeCaptiveRequests() {
  server_.on("/generate_204", HTTP_GET, [this]() { captiveRedirect(); });
  server_.on("/hotspot-detect.html", HTTP_GET, [this]() { captiveRedirect(); });
  server_.on("/connecttest.txt", HTTP_GET, [this]() { captiveRedirect(); });
  server_.on("/ncsi.txt", HTTP_GET, [this]() { captiveRedirect(); });
  server_.onNotFound([this]() {
    if (wifi_->accessPointActive()) captiveRedirect();
    else server_.send(404, "text/plain", "Not found");
  });
}

void ReflectorMicWebServer::captiveRedirect() {
  server_.sendHeader("Location", String("http://") + wifi_->ipAddress() + '/', true);
  server_.send(302, "text/plain", "ReflectorMic setup");
}

void ReflectorMicWebServer::linkReflector() {
  const String reflector = server_.hasArg("reflector")
                               ? server_.arg("reflector")
                               : store_->get().defaultReflector;
  const String module = server_.hasArg("module")
                            ? server_.arg("module")
                            : store_->get().defaultModule;
  const bool ok = reflector_->requestLink(reflector, module);
  server_.send(ok ? 200 : 409, "application/json",
               String("{\"ok\":") + (ok ? "true" : "false") +
                   ",\"status\":" + json(reflector_->statusText()) + "}");
}

void ReflectorMicWebServer::unlinkReflector() {
  reflector_->requestUnlink(true);
  server_.send(200, "application/json", "{\"ok\":true}");
}

void ReflectorMicWebServer::requestReboot() {
  server_.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(200);
}
