#pragma once

#ifndef SIMULATOR

#include <WiFi.h>

#include <cstdint>
#include <string>

// A deliberately tiny read-only HTTP file server, used so a DLNA renderer can
// pull a track straight off this device's SD card.
//
// Why not reuse CrossPointWebServer: that one is owned and pumped by the Web
// Server activity (which is not running while a dynamic app is), and it brings
// the whole file-manager UI plus a WebSocket server along. Here the job is one
// route serving one file, and it must answer Range requests, which renderers
// issue routinely. Raw WiFiServer keeps that small and explicit.
//
// Route:  GET /m?p=<url-encoded absolute path>
// Answers 200 or, for a Range request, 206 with Content-Range. Only media
// extensions are served, and dotfiles are refused — the same posture as the
// main server's /download.
class DynAppMediaServer {
 public:
  ~DynAppMediaServer();

  // Publishes `absPath` and returns the URL a renderer on the LAN should
  // fetch. Starts the listener on first use. Empty on failure (no STA link,
  // unsupported type, missing file).
  std::string publish(const std::string& absPath);

  // Accept and answer at most one pending request. Cheap when idle; call it
  // from the host activity's loop.
  void handle();

  void stop();
  bool isRunning() const { return running_; }
  bool isPublishing() const { return running_ && !publishedPath_.empty(); }

 private:
  bool begin();
  void serve(WiFiClient& client, const std::string& path, uint32_t rangeStart, bool hasRange);
  void sendStatus(WiFiClient& client, int code, const char* text);

  WiFiServer server_{kPort};
  bool running_ = false;
  // The single published track. A renderer may re-request it (seek, restart),
  // so it stays available until the next publish() or stop().
  std::string publishedPath_;

  static constexpr uint16_t kPort = 8081;
};

#endif  // SIMULATOR
