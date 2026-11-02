#include "web_server.h"

#include <WiFi.h>

namespace {
constexpr int MAX_WIFI_NETWORKS = 40;
constexpr uint32_t WIFI_SCAN_RESULT_TTL_MS = 30000;
constexpr const char *WIFI_SCAN_PENDING = "{\"scanning\":true,\"networks\":[]}";
}  // namespace

void ReflectorMicWebServer::scanWifi() {
  serviceWifiScan();
  server_.sendHeader("Cache-Control", "no-store");
  if (!wifiScanPending_ && wifiScanResult_.isEmpty()) {
    // Arduino starts the radio scan without waiting for completion. Repeated
    // polls reuse that scan rather than restarting it.
    const int count = WiFi.scanNetworks(true, true, false, 180);
    wifiScanPending_ = count == WIFI_SCAN_RUNNING;
    if (!wifiScanPending_) finishWifiScan(count);
  }
  if (wifiScanPending_) {
    server_.send(202, "application/json", WIFI_SCAN_PENDING);
    return;
  }
  server_.send(wifiScanResponseCode_, "application/json", wifiScanResult_);
  // Consuming a result makes the next request a fresh scan.
  wifiScanResult_ = String();
}

void ReflectorMicWebServer::serviceWifiScan() {
  if (wifiScanPending_) {
    const int count = WiFi.scanComplete();
    if (count != WIFI_SCAN_RUNNING) finishWifiScan(count);
  } else if (!wifiScanResult_.isEmpty() &&
             static_cast<uint32_t>(millis() - wifiScanReadyAtMs_) >=
                 WIFI_SCAN_RESULT_TTL_MS) {
    // A closed browser must not retain a response indefinitely.
    wifiScanResult_ = String();
  }
}

void ReflectorMicWebServer::finishWifiScan(int count) {
  wifiScanPending_ = false;
  wifiScanReadyAtMs_ = millis();
  wifiScanResponseCode_ = count < 0 ? 503 : 200;
  if (count < 0) {
    wifiScanResult_ =
        "{\"scanning\":false,\"networks\":[],\"error\":\"WiFi scan failed\"}";
  } else {
    wifiScanResult_ = "{\"scanning\":false,\"networks\":[";
    for (int i = 0; i < count && i < MAX_WIFI_NETWORKS; ++i) {
      if (i) wifiScanResult_ += ',';
      wifiScanResult_ += "{\"ssid\":" + json(WiFi.SSID(i)) +
                         ",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    wifiScanResult_ += "]}";
  }
  // Release SDK results even if the browser never polls again, including
  // zero-result and failed scans (scanComplete also reports scan timeouts).
  WiFi.scanDelete();
}
