#pragma once

#include <Arduino.h>

struct ReflectorHost {
  String name;
  String type;
  String ipv4;
};

struct DirectoryInfo {
  size_t bytes = 0;
  uint32_t totalEntries = 0;
  uint32_t refEntries = 0;
  uint32_t supportedEntries = 0;
};

class ReflectorDirectory {
 public:
  bool find(const String &name, ReflectorHost &host) const;
  String searchJson(String query, uint16_t limit) const;
  bool inspect(const char *path, DirectoryInfo &info, String &error) const;
  bool installUploadedFile(DirectoryInfo &info, String &error) const;
  void recoverInterruptedUpdate() const;

  static const char *hostsPath() { return "/DStar_Hosts.json"; }
  static const char *uploadPath() { return "/DStar_Hosts.upload"; }

 private:
  bool ensureCache() const;
  static String jsonField(const String &line);
  static const char *backupPath() { return "/DStar_Hosts.backup"; }
};
