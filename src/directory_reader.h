#pragma once
#include <Arduino.h>
#include <FS.h>

// LittleFS readStringUntil() performs a filesystem read for every character.
// Buffer those reads so startup/upload validation doesn't spend seconds in VFS.
class DirectoryLineReader {
 public:
  explicit DirectoryLineReader(File& file) : file_(file) {}
  bool available() {
    return !failed_ && (offset_ < size_ || file_.available());
  }
  bool ok() const { return !failed_; }
  String readLine() {
    String line;
    line.reserve(128);
    while (available()) {
      if (offset_ == size_) {
        size_ = file_.read(buffer_, sizeof(buffer_));
        offset_ = 0;
        if (!size_) {
          failed_ = true;
          break;
        }
      }
      const char c = static_cast<char>(buffer_[offset_++]);
      if (c == '\n') break;
      // Pi-Star records use short lines. Bound memory for malformed uploads.
      if (line.length() >= 1024) {
        failed_ = true;
        break;
      }
      line += c;
    }
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    return line;
  }

 private:
  File& file_;
  uint8_t buffer_[1024]{};
  size_t offset_ = 0, size_ = 0;
  bool failed_ = false;
};
