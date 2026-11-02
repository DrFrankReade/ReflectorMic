#include "wifi_manager.h"

#include <ESPmDNS.h>

namespace {

constexpr uint32_t STA_ATTEMPT_TIMEOUT_MS = 20000;
constexpr uint32_t MAX_RETRY_DELAY_MS = 60000;
constexpr uint32_t AP_SHUTDOWN_GRACE_MS = 15000;
constexpr uint8_t DNS_PORT = 53;

String generatedApSsid() {
  const uint32_t id = static_cast<uint32_t>(ESP.getEfuseMac());
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "ReflectorMic-%06lX",
           static_cast<unsigned long>(id & 0xffffffU));
  return String(buffer);
}

}  // namespace

void WifiManager::begin(const AppConfig &config) {
  config_ = config;
  apSsid_ = generatedApSsid();
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setHostname("ReflectorMic");

  if (config_.wifiSsid.isEmpty()) {
    startAccessPoint();
  } else {
    // Station association always gets first priority at power-up. The setup AP
    // appears only if that attempt fails, while background retries continue.
    startStationAttempt();
  }
}

void WifiManager::setConfig(const AppConfig &config) {
  config_ = config;
}

void WifiManager::startStationAttempt() {
  if (config_.wifiSsid.isEmpty()) return;
  WiFi.mode(apActive_ ? WIFI_AP_STA : WIFI_STA);
  WiFi.setHostname("ReflectorMic");
  WiFi.disconnect(false, false);
  if (config_.wifiOpen) {
    WiFi.begin(config_.wifiSsid.c_str());
  } else {
    WiFi.begin(config_.wifiSsid.c_str(), config_.wifiPassword.c_str());
  }
  attemptStartedMs_ = millis();
  nextAttemptMs_ = 0;
  Serial.printf("[wifi] connecting to %s\n", config_.wifiSsid.c_str());
}

void WifiManager::startAccessPoint() {
  if (apActive_) return;
  WiFi.mode(WIFI_AP_STA);
  const IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  apActive_ = WiFi.softAP(apSsid_.c_str(), accessPointPassword(), 6, false, 4);
  if (apActive_) {
    dns_.start(DNS_PORT, "*", ip);
    Serial.printf("[wifi] setup AP %s at %s\n", apSsid_.c_str(), ip.toString().c_str());
  }
}

void WifiManager::stopAccessPoint() {
  if (!apActive_) return;
  dns_.stop();
  WiFi.softAPdisconnect(true);
  apActive_ = false;
  WiFi.mode(WIFI_STA);
}

void WifiManager::startMdns() {
  if (mdnsActive_) return;
  if (MDNS.begin("reflectormic")) {
    MDNS.addService("http", "tcp", 80);
    mdnsActive_ = true;
  }
}

void WifiManager::service() {
  if (apActive_) dns_.processNextRequest();

  const uint32_t now = millis();
  const bool isConnected = connected();
  if (isConnected && !lastConnected_) {
    retryDelayMs_ = 2000;
    connectedAtMs_ = now;
    startMdns();
    Serial.printf("[wifi] connected, ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else if (!isConnected && lastConnected_) {
    mdnsActive_ = false;
    MDNS.end();
    nextAttemptMs_ = now + retryDelayMs_;
    Serial.println("[wifi] connection lost; reconnect scheduled");
  }
  lastConnected_ = isConnected;

  if (isConnected) {
    if (apActive_ && now - connectedAtMs_ >= AP_SHUTDOWN_GRACE_MS) {
      stopAccessPoint();
    }
    return;
  }

  if (config_.wifiSsid.isEmpty()) {
    startAccessPoint();
    return;
  }

  if (attemptStartedMs_ != 0 && now - attemptStartedMs_ >= STA_ATTEMPT_TIMEOUT_MS) {
    attemptStartedMs_ = 0;
    startAccessPoint();
    nextAttemptMs_ = now + retryDelayMs_;
    retryDelayMs_ = min(retryDelayMs_ * 2U, MAX_RETRY_DELAY_MS);
  }

  if (attemptStartedMs_ == 0 && nextAttemptMs_ != 0 &&
      static_cast<int32_t>(now - nextAttemptMs_) >= 0) {
    startStationAttempt();
  }
}

String WifiManager::ipAddress() const {
  if (connected()) return WiFi.localIP().toString();
  if (apActive_) return WiFi.softAPIP().toString();
  return "0.0.0.0";
}

String WifiManager::activeSsid() const {
  if (connected()) return WiFi.SSID();
  return apActive_ ? apSsid_ : String();
}

