#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

#include "app_config.h"
#include "audio_engine.h"
#include "build_features.h"
#include "echo_test.h"
#include "reflector_client.h"
#include "reflector_directory.h"
#include "wifi_manager.h"

class ReflectorMicWebServer {
 public:
  void begin(ConfigStore &store, WifiManager &wifi,
             ReflectorClient &reflector, AudioEngine &audio, EchoTest &echo);
  void service();
  void setPttPressed(bool pressed) { pttPressed_ = pressed; }

 private:
  void routeRequests();
  void routeAudioRequests();
  void routeDirectoryRequests();
  void routeCaptiveRequests();
  void sendConfig();
  void saveConfig();
  void sendStatus();
  void appendConnectionStatus(String &out) const;
#if REFLECTORMIC_DIAGNOSTICS
  void appendReflectorCounters(String &out) const;
  void appendAudioDiagnostics(String &out) const;
#endif
  void scanWifi();
  void serviceWifiScan();
  void finishWifiScan(int count);
  void listReflectors();
  void sendDirectoryStatus();
  void handleDirectoryUpload();
  void startDirectoryUpload();
  void writeDirectoryUpload(const HTTPUpload &upload);
  void discardDirectoryUpload();
  void finishDirectoryUpload();
  void armEcho();
  void cancelEcho();
  void testSpeaker();
  void setSpeakerVolume();
  void setSpeakerFilters();
  void recoverRx();
#if REFLECTORMIC_DIAGNOSTICS
  void testCodec();
  void sendAudioProfile();
  void captureRxSnapshot();
  void sendRxTrace();
#endif
  void linkReflector();
  void unlinkReflector();
  void requestReboot();
  void scheduleReboot(uint32_t delayMs);
  void captiveRedirect();
  static String json(const String &value);
  static bool boolArg(const String &value);

  WebServer server_{80};
  ConfigStore *store_ = nullptr;
  WifiManager *wifi_ = nullptr;
  ReflectorClient *reflector_ = nullptr;
  AudioEngine *audio_ = nullptr;
  EchoTest *echo_ = nullptr;
  ReflectorDirectory directory_;
  File directoryUploadFile_;
  size_t directoryUploadBytes_ = 0;
  bool directoryUploadFinished_ = false;
  bool directoryUploadOk_ = false;
  String directoryUploadError_;
  DirectoryInfo uploadedDirectoryInfo_;
  bool pttPressed_ = false;
  bool wifiScanPending_ = false;
  String wifiScanResult_;
  int wifiScanResponseCode_ = 200;
  uint32_t wifiScanReadyAtMs_ = 0;
  bool rebootPending_ = false;
  uint32_t rebootRequestedAtMs_ = 0;
  uint32_t rebootDelayMs_ = 0;
};
