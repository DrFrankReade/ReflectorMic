#include "web_server.h"

#include <LittleFS.h>

void ReflectorMicWebServer::listReflectors() {
  const String query = server_.hasArg("q") ? server_.arg("q") : String();
  const uint16_t limit = server_.hasArg("limit") ? server_.arg("limit").toInt() : 80;
  server_.send(200, "application/json", directory_.searchJson(query, limit));
}

void ReflectorMicWebServer::sendDirectoryStatus() {
  DirectoryInfo info;
  String error;
  const bool valid = directory_.inspect(ReflectorDirectory::hostsPath(), info, error);
  String out = "{";
  out += "\"valid\":" + String(valid ? "true" : "false") + ',';
  out += "\"mounted\":" + String(LittleFS.totalBytes() > 0 ? "true" : "false") + ',';
  out += "\"bytes\":" + String(info.bytes) + ',';
  out += "\"entries\":" + String(info.totalEntries) + ',';
  out += "\"supportedEntries\":" + String(info.supportedEntries) + ',';
  out += "\"refEntries\":" + String(info.refEntries) + ',';
  out += "\"fsTotal\":" + String(LittleFS.totalBytes()) + ',';
  out += "\"fsUsed\":" + String(LittleFS.usedBytes()) + ',';
  out += "\"error\":" + json(error);
  out += '}';
  server_.send(valid ? 200 : 404, "application/json", out);
}

void ReflectorMicWebServer::discardDirectoryUpload() {
  if (directoryUploadFile_) directoryUploadFile_.close();
  LittleFS.remove(ReflectorDirectory::uploadPath());
}

void ReflectorMicWebServer::startDirectoryUpload() {
  discardDirectoryUpload();
  directoryUploadBytes_ = 0;
  directoryUploadFinished_ = false;
  directoryUploadOk_ = false;
  directoryUploadError_ = "";
  uploadedDirectoryInfo_ = DirectoryInfo();
  if (LittleFS.totalBytes() == 0) {
    directoryUploadError_ = "LittleFS is not mounted";
    return;
  }
  // Ignore the browser filename. Validation installs only this fixed path.
  directoryUploadFile_ = LittleFS.open(ReflectorDirectory::uploadPath(), "w");
  if (!directoryUploadFile_) directoryUploadError_ = "unable to open upload file";
}

void ReflectorMicWebServer::writeDirectoryUpload(const HTTPUpload &upload) {
  static constexpr size_t MAX_DIRECTORY_BYTES = 1250U * 1024U;
  if (!directoryUploadError_.isEmpty()) return;
  if (upload.currentSize > MAX_DIRECTORY_BYTES - directoryUploadBytes_) {
    directoryUploadError_ = "directory exceeds the 1.25 MB upload limit";
    discardDirectoryUpload();
    return;
  }
  const size_t written = directoryUploadFile_.write(upload.buf, upload.currentSize);
  if (written != upload.currentSize) {
    directoryUploadError_ = "LittleFS write failed";
    discardDirectoryUpload();
    return;
  }
  directoryUploadBytes_ += written;
}

void ReflectorMicWebServer::handleDirectoryUpload() {
  HTTPUpload &upload = server_.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      startDirectoryUpload();
      break;
    case UPLOAD_FILE_WRITE:
      writeDirectoryUpload(upload);
      break;
    case UPLOAD_FILE_END:
      if (directoryUploadFile_) directoryUploadFile_.close();
      directoryUploadFinished_ = true;
      if (directoryUploadError_.isEmpty()) {
        directoryUploadOk_ = directory_.installUploadedFile(
            uploadedDirectoryInfo_, directoryUploadError_);
      }
      break;
    case UPLOAD_FILE_ABORTED:
      discardDirectoryUpload();
      directoryUploadFinished_ = true;
      directoryUploadOk_ = false;
      directoryUploadError_ = "upload aborted";
      break;
  }
}

void ReflectorMicWebServer::finishDirectoryUpload() {
  if (!directoryUploadFinished_) {
    server_.send(400, "application/json", "{\"error\":\"no file uploaded\"}");
    return;
  }
  if (!directoryUploadOk_) {
    server_.send(400, "application/json",
                 String("{\"error\":") + json(directoryUploadError_) + "}");
    directoryUploadFinished_ = false;
    return;
  }
  reflector_->directoryUpdated();
  String out = "{\"ok\":true,\"bytes\":" +
               String(uploadedDirectoryInfo_.bytes) +
               ",\"entries\":" + String(uploadedDirectoryInfo_.totalEntries) +
               ",\"refEntries\":" + String(uploadedDirectoryInfo_.refEntries) +
               ",\"supportedEntries\":" + String(uploadedDirectoryInfo_.supportedEntries) + "}";
  server_.send(200, "application/json", out);
  directoryUploadFinished_ = false;
  directoryUploadOk_ = false;
}
