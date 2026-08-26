#ifndef SIMULATOR

#include "DynAppMediaServer.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "util/TaskWatchdog.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr size_t kStreamChunk = 1460;  // one TCP segment's worth
constexpr uint32_t kHeaderTimeoutMs = 2000;

struct MediaType {
  const char* ext;
  const char* mime;
};

// Only these are served. Renderers reject what they cannot decode anyway, and
// the allow-list keeps this from becoming a general file-exfiltration route.
constexpr MediaType kTypes[] = {
    {".mp3", "audio/mpeg"},  {".m4a", "audio/mp4"},   {".aac", "audio/aac"},   {".wav", "audio/wav"},
    {".flac", "audio/flac"}, {".ogg", "audio/ogg"},   {".opus", "audio/opus"}, {".wma", "audio/x-ms-wma"},
    {".mp4", "video/mp4"},   {".m4b", "audio/mp4"},
};

const char* mimeFor(const std::string& path) {
  for (const auto& type : kTypes) {
    if (FsHelpers::checkFileExtension(path, type.ext)) return type.mime;
  }
  return nullptr;
}

std::string urlEncode(const std::string& in) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(in.size() + 16);
  for (const unsigned char c : in) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                      c == '_' || c == '.' || c == '~' || c == '/';
    if (safe) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const auto hexVal = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = hexVal(in[i + 1]), lo = hexVal(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(in[i] == '+' ? ' ' : in[i]);
  }
  return out;
}

}  // namespace

DynAppMediaServer::~DynAppMediaServer() { stop(); }

bool DynAppMediaServer::begin() {
  if (running_) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  server_.begin();
  server_.setNoDelay(true);
  running_ = true;
  LOG_INF("DYNMEDIA", "serving on %s:%u", WiFi.localIP().toString().c_str(), static_cast<unsigned>(kPort));
  return true;
}

void DynAppMediaServer::stop() {
  if (!running_) return;
  server_.end();
  running_ = false;
  publishedPath_.clear();
}

std::string DynAppMediaServer::publish(const std::string& absPath) {
  if (absPath.empty() || absPath[0] != '/') return {};
  if (absPath.find("..") != std::string::npos) return {};
  if (mimeFor(absPath) == nullptr) return {};
  const std::string leaf = absPath.substr(absPath.rfind('/') + 1);
  if (leaf.empty() || leaf[0] == '.') return {};
  if (!Storage.exists(absPath.c_str())) return {};
  if (!begin()) return {};

  publishedPath_ = absPath;
  char url[320];
  snprintf(url, sizeof(url), "http://%s:%u/m?p=%s", WiFi.localIP().toString().c_str(), static_cast<unsigned>(kPort),
           urlEncode(absPath).c_str());
  return std::string(url);
}

void DynAppMediaServer::sendStatus(WiFiClient& client, const int code, const char* text) {
  char buf[96];
  snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n", code, text);
  client.print(buf);
}

void DynAppMediaServer::handle() {
  if (!running_) return;
  WiFiClient client = server_.available();
  if (!client) return;

  // Read the request head only (renderers send no body on GET/HEAD).
  std::string head;
  head.reserve(512);
  const uint32_t deadline = millis() + kHeaderTimeoutMs;
  while (client.connected() && millis() < deadline) {
    while (client.available()) {
      const char c = static_cast<char>(client.read());
      if (head.size() < 2048) head.push_back(c);
      if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0) goto parsed;
    }
    resetTaskWatchdogIfSubscribed();
    delay(2);
  }
parsed:
  if (head.empty()) {
    client.stop();
    return;
  }

  const bool isHead = head.compare(0, 5, "HEAD ") == 0;
  const bool isGet = head.compare(0, 4, "GET ") == 0;
  if (!isGet && !isHead) {
    sendStatus(client, 405, "Method Not Allowed");
    client.stop();
    return;
  }

  // Request target: "GET /m?p=... HTTP/1.1"
  const size_t pathStart = head.find(' ') + 1;
  const size_t pathEnd = head.find(' ', pathStart);
  const std::string target = head.substr(pathStart, pathEnd - pathStart);
  const size_t q = target.find("?p=");
  if (target.compare(0, 3, "/m?") != 0 || q == std::string::npos) {
    sendStatus(client, 404, "Not Found");
    client.stop();
    return;
  }
  const std::string wanted = urlDecode(target.substr(q + 3));

  // Only the currently published track is reachable, so a stray request can
  // never walk the card.
  if (wanted != publishedPath_) {
    sendStatus(client, 403, "Forbidden");
    client.stop();
    return;
  }

  // Range: bytes=START-  (renderers use this to seek and to resume)
  uint32_t rangeStart = 0;
  bool hasRange = false;
  const size_t rangePos = head.find("Range: bytes=");
  if (rangePos == std::string::npos) {
    const size_t lower = head.find("range: bytes=");
    if (lower != std::string::npos) {
      rangeStart = static_cast<uint32_t>(strtoul(head.c_str() + lower + 13, nullptr, 10));
      hasRange = true;
    }
  } else {
    rangeStart = static_cast<uint32_t>(strtoul(head.c_str() + rangePos + 13, nullptr, 10));
    hasRange = true;
  }

  if (isHead) {
    HalFile probe;
    if (!Storage.openFileForRead("DYNMEDIA", wanted, probe)) {
      sendStatus(client, 404, "Not Found");
      client.stop();
      return;
    }
    const uint32_t total = static_cast<uint32_t>(probe.fileSize());
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nAccept-Ranges: bytes\r\n"
             "Connection: close\r\n\r\n",
             mimeFor(wanted), static_cast<unsigned>(total));
    client.print(hdr);
    client.stop();
    return;
  }
  serve(client, wanted, rangeStart, hasRange);
}

void DynAppMediaServer::serve(WiFiClient& client, const std::string& path, const uint32_t rangeStart,
                              const bool hasRange) {
  HalFile file;
  if (!Storage.openFileForRead("DYNMEDIA", path, file)) {
    sendStatus(client, 404, "Not Found");
    client.stop();
    return;
  }
  const uint32_t total = static_cast<uint32_t>(file.fileSize());
  if (hasRange && rangeStart >= total) {
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%u\r\nConnection: close\r\n\r\n",
             static_cast<unsigned>(total));
    client.print(hdr);
    client.stop();
    return;
  }
  const uint32_t start = hasRange ? rangeStart : 0;
  const uint32_t remaining = total - start;

  char hdr[288];
  if (hasRange) {
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
             "Content-Range: bytes %u-%u/%u\r\nAccept-Ranges: bytes\r\nConnection: close\r\n\r\n",
             mimeFor(path), static_cast<unsigned>(remaining), static_cast<unsigned>(start),
             static_cast<unsigned>(total - 1), static_cast<unsigned>(total));
  } else {
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nAccept-Ranges: bytes\r\n"
             "Connection: close\r\n\r\n",
             mimeFor(path), static_cast<unsigned>(total));
  }
  client.print(hdr);

  if (start > 0 && !file.seekSet(start)) {
    client.stop();
    return;
  }
  // One transient chunk buffer for the whole transfer, freed on return.
  auto buf = makeUniqueNoThrow<uint8_t[]>(kStreamChunk);
  if (!buf) {
    LOG_ERR("DYNMEDIA", "OOM: stream buffer");
    client.stop();
    return;
  }
  // A whole track streams inside this call, so the loop has to keep the
  // watchdog fed and let other tasks (Wi-Fi, input) run.
  uint32_t sent = 0;
  int chunks = 0;
  while (sent < remaining && client.connected()) {
    const size_t want = std::min<size_t>(kStreamChunk, remaining - sent);
    const int got = file.read(buf.get(), want);
    if (got <= 0) break;
    const size_t wrote = client.write(buf.get(), static_cast<size_t>(got));
    if (wrote == 0) break;  // renderer hung up mid-track (skip/stop)
    sent += static_cast<uint32_t>(wrote);
    if ((++chunks & 0x07) == 0) {
      resetTaskWatchdogIfSubscribed();
      yield();
    }
  }
  client.stop();
}

#endif  // SIMULATOR
