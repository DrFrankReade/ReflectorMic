#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "speaker_filters.h"

struct AppConfig {
  String callsign = "N0CALL";
  String localModule = "D";
  String mySuffix = "RMIC";
  String defaultReflector = "REF001";
  String defaultModule = "C";
  String startupMode = "last";  // last, default, or manual

  uint16_t txTimeoutSec = 180;
  uint8_t speakerVolume = 160;
  float micGain = 1.0f;
  SpeakerFilterSettings speakerFilters;

  String wifiSsid;
  String wifiPassword;
  bool wifiOpen = false;
};

class ConfigStore {
 public:
  bool begin();
  AppConfig &get() { return config_; }
  const AppConfig &get() const { return config_; }

  bool save();
  bool saveSpeakerFilters(const SpeakerFilterSettings& filters);
  void normalize();

  String lastReflector();
  bool saveLastReflector(const String &target);
  void clearWifi();

 private:
  Preferences prefs_;
  AppConfig config_;
};

String normalizeCallsign(String value);
String normalizeReflector(String value);
String normalizeModule(String value, const char *fallback);
bool isValidCallsign(const String &value);
bool isValidReflector(const String &value);
