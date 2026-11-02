#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include "app_config.h"

class WifiManager {
 public:
  void begin(const AppConfig &config);
  void service();

  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  bool accessPointActive() const { return apActive_; }
  String ipAddress() const;
  String activeSsid() const;
  String accessPointSsid() const { return apSsid_; }
  const char *accessPointPassword() const { return "reflectormic"; }

  // Called after settings are saved. A reboot is preferred, but this also
  // lets tests and future callers replace the in-memory credentials safely.
  void setConfig(const AppConfig &config);

 private:
  void startStationAttempt();
  void startAccessPoint();
  void stopAccessPoint();
  void startMdns();

  AppConfig config_;
  DNSServer dns_;
  String apSsid_;
  bool apActive_ = false;
  bool mdnsActive_ = false;
  bool lastConnected_ = false;
  uint32_t attemptStartedMs_ = 0;
  uint32_t nextAttemptMs_ = 0;
  uint32_t retryDelayMs_ = 2000;
  uint32_t connectedAtMs_ = 0;
};

