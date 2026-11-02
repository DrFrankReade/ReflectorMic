#include "web_server.h"

void ReflectorMicWebServer::begin(ConfigStore &store, WifiManager &wifi,
                                 ReflectorClient &reflector, AudioEngine &audio,
                                 EchoTest &echo) {
  store_ = &store;
  wifi_ = &wifi;
  reflector_ = &reflector;
  audio_ = &audio;
  echo_ = &echo;
  directory_.recoverInterruptedUpdate();
  routeRequests();
  server_.begin();
}

void ReflectorMicWebServer::service() {
  // Requests only schedule work. Keep servicing the main loop while the radio
  // scans or an HTTP response drains before a restart.
  serviceWifiScan();
  if (rebootPending_ &&
      static_cast<uint32_t>(millis() - rebootRequestedAtMs_) >= rebootDelayMs_) {
    rebootPending_ = false;
    ESP.restart();
    return;
  }
  server_.handleClient();
}

void ReflectorMicWebServer::scheduleReboot(uint32_t delayMs) {
  // Repeated requests must not keep postponing a restart indefinitely.
  if (rebootPending_) return;
  rebootRequestedAtMs_ = millis();
  rebootDelayMs_ = delayMs;
  rebootPending_ = true;
}

String ReflectorMicWebServer::json(const String &value) {
  String out = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '\\' || c == '"') out += '\\';
    if (c == '\n') out += "\\n";
    else if (c >= 0x20) out += c;
  }
  out += '"';
  return out;
}

bool ReflectorMicWebServer::boolArg(const String &rawValue) {
  String value = rawValue;
  value.toLowerCase();
  return value == "1" || value == "true" || value == "on" || value == "yes";
}
