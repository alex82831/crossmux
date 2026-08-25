#include "NetKit.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>

#include "network/HttpDownloader.h"

namespace netkit {

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

void teardownWifi() {
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();
}

bool fetchToString(const std::string& url, std::string& out, const size_t maxBytes) {
  out.clear();
  // One bounded reservation (≤4KB) instead of repeated append regrowth; the
  // callers cap responses at a few KB so this covers the common whole body.
  out.reserve(std::min<size_t>(maxBytes, 4096));
  bool overflow = false;
  const bool ok = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, const size_t len) {
    if (out.size() + len > maxBytes) {
      overflow = true;
      return false;
    }
    out.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  return ok && !overflow;
}

}  // namespace netkit
