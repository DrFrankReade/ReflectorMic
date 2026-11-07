#include "web_server.h"

void ReflectorMicWebServer::sendConfig() {
  const AppConfig &c = store_->get();
  String out = "{";
  out += "\"callsign\":" + json(c.callsign) + ',';
  out += "\"localModule\":" + json(c.localModule) + ',';
  out += "\"mySuffix\":" + json(c.mySuffix) + ',';
  out += "\"defaultReflector\":" + json(c.defaultReflector) + ',';
  out += "\"defaultModule\":" + json(c.defaultModule) + ',';
  out += "\"startupMode\":" + json(c.startupMode) + ',';
  out += "\"txTimeoutSec\":" + String(c.txTimeoutSec) + ',';
  out += "\"speakerVolume\":" + String(c.speakerVolume) + ',';
  out += "\"micGain\":" + String(c.micGain, 2) + ',';
  out += "\"speakerFiltersEnabled\":" + String(c.speakerFilters.enabled ? "true" : "false") + ',';
  out += "\"speakerLowCutHz\":" + String(c.speakerFilters.lowCutHz) + ',';
  out += "\"speakerFilterSlopeDb\":" + String(c.speakerFilters.slopeDb) + ',';
  out += "\"speakerHighCutHz\":" + String(c.speakerFilters.highCutHz) + ',';
  out += "\"wifiSsid\":" + json(c.wifiSsid) + ',';
  out += "\"wifiOpen\":" + String(c.wifiOpen ? "true" : "false");
  out += '}';
  server_.send(200, "application/json", out);
}

void ReflectorMicWebServer::saveConfig() {
  AppConfig &c = store_->get();
  const AppConfig previous = c;
  if (server_.hasArg("callsign")) c.callsign = server_.arg("callsign");
  if (server_.hasArg("localModule")) c.localModule = server_.arg("localModule");
  if (server_.hasArg("mySuffix")) c.mySuffix = server_.arg("mySuffix");
  if (server_.hasArg("defaultReflector")) c.defaultReflector = server_.arg("defaultReflector");
  if (server_.hasArg("defaultModule")) c.defaultModule = server_.arg("defaultModule");
  if (server_.hasArg("startupMode")) c.startupMode = server_.arg("startupMode");
  if (server_.hasArg("txTimeoutSec")) c.txTimeoutSec = server_.arg("txTimeoutSec").toInt();
  if (server_.hasArg("speakerVolume")) c.speakerVolume = server_.arg("speakerVolume").toInt();
  if (server_.hasArg("micGain")) c.micGain = server_.arg("micGain").toFloat();
  if (server_.hasArg("wifiSsid")) c.wifiSsid = server_.arg("wifiSsid");
  if (server_.hasArg("wifiPassword") && !server_.arg("wifiPassword").isEmpty()) {
    c.wifiPassword = server_.arg("wifiPassword");
  }
  if (server_.hasArg("wifiOpen")) c.wifiOpen = boolArg(server_.arg("wifiOpen"));

  store_->normalize();
  if (!store_->save()) {
    // ConfigStore writes individual NVS keys; it cannot roll those back here.
    // Keep the running configuration consistent with the active subsystems.
    c = previous;
    server_.send(500, "application/json", "{\"error\":\"unable to save settings\"}");
    return;
  }
  audio_->setLevels(c.speakerVolume, c.micGain);
  wifi_->setConfig(c);
  reflector_->updateConfig();
  const bool reboot = server_.hasArg("reboot") && boolArg(server_.arg("reboot"));
  server_.send(200, "application/json", reboot
                   ? "{\"ok\":true,\"rebooting\":true}"
                   : "{\"ok\":true}");
  if (reboot) scheduleReboot(250);
}
