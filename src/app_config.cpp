#include "app_config.h"
#include "reflector_protocol.h"

namespace {

constexpr const char *PREF_NAMESPACE = "reflectormic";

String upperTrimmed(String value) {
  value.trim();
  value.toUpperCase();
  return value;
}

}  // namespace

String normalizeCallsign(String value) {
  value = upperTrimmed(value);
  String out;
  for (size_t i = 0; i < value.length() && out.length() < 7; ++i) {
    const char c = value[i];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/') {
      out += c;
    }
  }
  return out.isEmpty() ? String("N0CALL") : out;
}

String normalizeReflector(String value) {
  value = upperTrimmed(value);
  return value;
}

String normalizeModule(String value, const char *fallback) {
  value = upperTrimmed(value);
  if (value.length() != 1 || value[0] < 'A' || value[0] > 'Z') {
    return String(fallback);
  }
  return value;
}

bool isValidCallsign(const String &value) {
  const String call = normalizeCallsign(value);
  return call != "N0CALL" && call.length() >= 3 && call.length() <= 7;
}

bool isValidReflector(const String &value) {
  const String reflector = normalizeReflector(value);
  return reflector_wire::supportedName(reflector.c_str());
}

bool ConfigStore::begin() {
  if (!prefs_.begin(PREF_NAMESPACE, false)) return false;

  config_.callsign = prefs_.getString("callsign", config_.callsign);
  config_.localModule = prefs_.getString("localmod", config_.localModule);
  config_.mySuffix = prefs_.getString("suffix", config_.mySuffix);
  config_.defaultReflector = prefs_.getString("reflector", config_.defaultReflector);
  config_.defaultModule = prefs_.getString("refmod", config_.defaultModule);
  config_.startupMode = prefs_.getString("startup", config_.startupMode);
  config_.txTimeoutSec = prefs_.getUShort("txtimeout", config_.txTimeoutSec);
  config_.speakerVolume = prefs_.getUChar("volume", config_.speakerVolume);
  config_.micGain = prefs_.getFloat("micgain", config_.micGain);
  config_.wifiSsid = prefs_.getString("ssid", "");
  config_.wifiPassword = prefs_.getString("wifipass", "");
  config_.wifiOpen = prefs_.getBool("wifiopen", false);
  config_.speakerFilters = SpeakerFilterSettings::unpack(
      prefs_.getUInt("spkfilters", SpeakerFilterSettings{}.packed()));
  normalize();
  return true;
}

void ConfigStore::normalize() {
  config_.callsign = normalizeCallsign(config_.callsign);
  config_.localModule = normalizeModule(config_.localModule, "D");
  config_.mySuffix = upperTrimmed(config_.mySuffix).substring(0, 4);
  config_.defaultReflector = normalizeReflector(config_.defaultReflector);
  if (!isValidReflector(config_.defaultReflector)) config_.defaultReflector = "REF001";
  config_.defaultModule = normalizeModule(config_.defaultModule, "C");

  config_.startupMode.toLowerCase();
  if (config_.startupMode != "last" && config_.startupMode != "default") {
    config_.startupMode = "manual";
  }
  config_.txTimeoutSec = constrain(config_.txTimeoutSec, static_cast<uint16_t>(15),
                                   static_cast<uint16_t>(600));
  config_.speakerVolume = constrain(config_.speakerVolume, static_cast<uint8_t>(8),
                                    static_cast<uint8_t>(255));
  if (!config_.speakerFilters.valid()) config_.speakerFilters = {};
  config_.micGain = constrain(config_.micGain, 0.25f, 4.0f);
  config_.wifiSsid = config_.wifiSsid.substring(0, 32);
  config_.wifiPassword = config_.wifiPassword.substring(0, 64);
}

bool ConfigStore::save() {
  normalize();
  bool ok = true;
  // Preferences::putString() legitimately returns zero for an empty string.
  // MYCALL suffix and Wi-Fi password are optional, so treating that return as
  // a write failure made every normal station-settings save report HTTP 500
  // after partially updating NVS.
  ok &= prefs_.putString("callsign", config_.callsign) == config_.callsign.length();
  ok &= prefs_.putString("localmod", config_.localModule) == config_.localModule.length();
  ok &= prefs_.putString("suffix", config_.mySuffix) == config_.mySuffix.length();
  ok &= prefs_.putString("reflector", config_.defaultReflector) ==
        config_.defaultReflector.length();
  ok &= prefs_.putString("refmod", config_.defaultModule) ==
        config_.defaultModule.length();
  ok &= prefs_.putString("startup", config_.startupMode) == config_.startupMode.length();
  ok &= prefs_.putUShort("txtimeout", config_.txTimeoutSec) == sizeof(uint16_t);
  ok &= prefs_.putUChar("volume", config_.speakerVolume) == sizeof(uint8_t);
  ok &= prefs_.putFloat("micgain", config_.micGain) == sizeof(float);
  ok &= prefs_.putString("ssid", config_.wifiSsid) == config_.wifiSsid.length();
  ok &= prefs_.putString("wifipass", config_.wifiPassword) ==
        config_.wifiPassword.length();
  ok &= prefs_.putBool("wifiopen", config_.wifiOpen) == sizeof(bool);
  return ok;
}

String ConfigStore::lastReflector() {
  return prefs_.getString("lastref", "");
}

bool ConfigStore::saveLastReflector(const String &target) {
  return prefs_.putString("lastref", target) > 0;
}

void ConfigStore::clearWifi() {
  config_.wifiSsid = "";
  config_.wifiPassword = "";
  config_.wifiOpen = false;
  save();
}

// A single NVS key keeps the three filter settings consistent across resets.
bool ConfigStore::saveSpeakerFilters(const SpeakerFilterSettings& filters) {
  if (!filters.valid() || prefs_.putUInt("spkfilters", filters.packed()) != sizeof(uint32_t))
    return false;
  config_.speakerFilters = filters;
  return true;
}
