#include "reflector_directory.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>

#include <cstring>

#include "directory_reader.h"
#include "reflector_protocol.h"

namespace {

// Directory calls run only on the application/core-1 loop. Both the web
// server and reflector client share this immutable index; no codec task uses
// it. Upload activation invalidates it before the next lookup.
struct CachedHost {
  char name[7];
  char type[4];
  char ipv4[16];
};
CachedHost* cachedHosts = nullptr;
DirectoryInfo cachedInfo;
size_t cachedCount = 0;
bool cacheValid = false;

void invalidateCache() {
  heap_caps_free(cachedHosts);
  cachedHosts = nullptr;
  cachedCount = 0;
  cacheValid = false;
}

String escapeJson(const String& value) {
  String out = "\"";
  out.reserve(value.length() + 4);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '\\' || c == '"') out += '\\';
    if (c >= 0x20) out += c;
  }
  out += '"';
  return out;
}

}  // namespace

String ReflectorDirectory::jsonField(const String& line) {
  const int colon = line.indexOf(':');
  if (colon < 0) return "";
  const int first = line.indexOf('"', colon + 1);
  const int second = first < 0 ? -1 : line.indexOf('"', first + 1);
  return (first >= 0 && second > first) ? line.substring(first + 1, second)
                                        : String();
}

bool ReflectorDirectory::find(const String& name, ReflectorHost& host) const {
  String wanted = name;
  wanted.trim();
  wanted.toUpperCase();
  if (ensureCache()) {
    for (size_t i = 0; i < cachedCount; ++i) {
      if (wanted != cachedHosts[i].name) continue;
      host.name = cachedHosts[i].name;
      host.type = cachedHosts[i].type;
      host.ipv4 = cachedHosts[i].ipv4;
      return true;
    }
    return false;
  }
  File file = LittleFS.open(hostsPath(), "r");
  if (!file) return false;
  DirectoryLineReader reader(file);

  ReflectorHost current;
  while (reader.available()) {
    String line = reader.readLine();
    line.trim();
    if (line.startsWith("\"name\""))
      current.name = jsonField(line);
    else if (line.startsWith("\"reflector_type\""))
      current.type = jsonField(line);
    else if (line.startsWith("\"ipv4\"")) {
      current.ipv4 = jsonField(line);
      if (current.name == wanted &&
          reflector_wire::supportedName(current.name.c_str())) {
        host = current;
        file.close();
        return true;
      }
      current = ReflectorHost();
    }
  }
  file.close();
  return false;
}

String ReflectorDirectory::searchJson(String query, uint16_t limit) const {
  query.trim();
  query.toUpperCase();
  limit =
      constrain(limit, static_cast<uint16_t>(1), static_cast<uint16_t>(150));
  if (ensureCache()) {
    String out = "{\"reflectors\":[";
    uint16_t count = 0;
    for (size_t i = 0; i < cachedCount && count < limit; ++i) {
      if (!query.isEmpty() && !strstr(cachedHosts[i].name, query.c_str()))
        continue;
      if (count++) out += ',';
      out += "{\"name\":" + escapeJson(cachedHosts[i].name) +
             ",\"ipv4\":" + escapeJson(cachedHosts[i].ipv4) + "}";
    }
    return out + "],\"count\":" + String(count) + "}";
  }
  File file = LittleFS.open(hostsPath(), "r");
  if (!file) return "{\"reflectors\":[],\"error\":\"directory missing\"}";
  DirectoryLineReader reader(file);

  String out = "{\"reflectors\":[";
  ReflectorHost current;
  uint16_t count = 0;
  while (reader.available() && count < limit) {
    String line = reader.readLine();
    line.trim();
    if (line.startsWith("\"name\""))
      current.name = jsonField(line);
    else if (line.startsWith("\"reflector_type\""))
      current.type = jsonField(line);
    else if (line.startsWith("\"ipv4\"")) {
      current.ipv4 = jsonField(line);
      if (reflector_wire::supportedName(current.name.c_str()) &&
          (query.isEmpty() || current.name.indexOf(query) >= 0)) {
        if (count++) out += ',';
        out += "{\"name\":" + escapeJson(current.name) +
               ",\"ipv4\":" + escapeJson(current.ipv4) + "}";
      }
      current = ReflectorHost();
    }
  }
  file.close();
  out += "],\"count\":" + String(count) + "}";
  return out;
}

bool ReflectorDirectory::inspect(const char* path, DirectoryInfo& info,
                                 String& error) const {
  info = DirectoryInfo();
  error = "";
  if (cacheValid && strcmp(path, hostsPath()) == 0) {
    info = cachedInfo;
    return true;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    error = "directory file is missing";
    return false;
  }
  info.bytes = file.size();
  if (info.bytes < 32) {
    file.close();
    error = "directory file is empty or truncated";
    return false;
  }

  bool sawReflectors = false;
  DirectoryLineReader reader(file);
  String previousNonempty;
  String lastNonempty;
  ReflectorHost current;
  while (reader.available()) {
    String line = reader.readLine();
    line.trim();
    if (!line.isEmpty()) {
      previousNonempty = lastNonempty;
      lastNonempty = line;
    }
    if (line.startsWith("\"reflectors\""))
      sawReflectors = true;
    else if (line.startsWith("\"name\""))
      current.name = jsonField(line);
    else if (line.startsWith("\"reflector_type\""))
      current.type = jsonField(line);
    else if (line.startsWith("\"ipv4\"")) {
      current.ipv4 = jsonField(line);
      IPAddress address;
      if (current.name.isEmpty() || current.type.isEmpty() ||
          !address.fromString(current.ipv4)) {
        file.close();
        error = "invalid reflector record near entry " +
                String(info.totalEntries + 1);
        return false;
      }
      info.totalEntries++;
      if (current.type == "REF") info.refEntries++;
      if (reflector_wire::supportedName(current.name.c_str()))
        info.supportedEntries++;
      current = ReflectorHost();
    }
  }
  file.close();

  if (!reader.ok() || !sawReflectors || previousNonempty != "]" ||
      lastNonempty != "}" || info.totalEntries == 0 ||
      info.supportedEntries == 0) {
    error =
        "not a complete Pi-Star DStar_Hosts.json file with REF, XRF, DCS or "
        "XLX entries";
    return false;
  }
  return true;
}

bool ReflectorDirectory::ensureCache() const {
  if (cacheValid) return true;
  DirectoryInfo info;
  String error;
  if (!inspect(hostsPath(), info, error)) return false;
  const size_t bytes = info.supportedEntries * sizeof(CachedHost);
  auto* records = static_cast<CachedHost*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // A damaged/unavailable PSRAM allocation must not disable directory access.
  if (!records) return false;  // Buffered, allocation-bounded fallback above.
  File file = LittleFS.open(hostsPath(), "r");
  if (!file) {
    heap_caps_free(records);
    return false;
  }
  DirectoryLineReader reader(file);
  ReflectorHost current;
  size_t count = 0;
  while (reader.available()) {
    String line = reader.readLine();
    line.trim();
    if (line.startsWith("\"name\""))
      current.name = jsonField(line);
    else if (line.startsWith("\"reflector_type\""))
      current.type = jsonField(line);
    else if (line.startsWith("\"ipv4\"")) {
      current.ipv4 = jsonField(line);
      if (reflector_wire::supportedName(current.name.c_str())) {
        if (count >= info.supportedEntries) {
          heap_caps_free(records);
          return false;
        }
        records[count] = CachedHost{};
        memcpy(records[count].name, current.name.c_str(), 6);
        memcpy(records[count].type, current.type.c_str(),
               min(current.type.length(), size_t(3)));
        memcpy(records[count].ipv4, current.ipv4.c_str(),
               min(current.ipv4.length(), size_t(15)));
        ++count;
      }
      current = ReflectorHost();
    }
  }
  file.close();
  if (!reader.ok() || count != info.supportedEntries) {
    heap_caps_free(records);
    return false;
  }
  cachedHosts = records;
  cachedCount = count;
  cachedInfo = info;
  cacheValid = true;
  return true;
}

bool ReflectorDirectory::installUploadedFile(DirectoryInfo& info,
                                             String& error) const {
  if (!inspect(uploadPath(), info, error)) {
    LittleFS.remove(uploadPath());
    return false;
  }

  LittleFS.remove(backupPath());
  const bool hadCurrent = LittleFS.exists(hostsPath());
  if (hadCurrent && !LittleFS.rename(hostsPath(), backupPath())) {
    LittleFS.remove(uploadPath());
    error = "unable to preserve the current directory";
    return false;
  }
  if (!LittleFS.rename(uploadPath(), hostsPath())) {
    if (hadCurrent) LittleFS.rename(backupPath(), hostsPath());
    error = "unable to activate the uploaded directory";
    return false;
  }
  LittleFS.remove(backupPath());
  invalidateCache();
  ensureCache();
  return true;
}

void ReflectorDirectory::recoverInterruptedUpdate() const {
  invalidateCache();
  if (!LittleFS.exists(hostsPath()) && LittleFS.exists(backupPath())) {
    LittleFS.rename(backupPath(), hostsPath());
  } else {
    LittleFS.remove(backupPath());
  }
  LittleFS.remove(uploadPath());
  // Once at boot, before the main loop starts servicing real-time traffic.
  ensureCache();
}
