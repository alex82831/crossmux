#ifndef SIMULATOR

#include "DynAppApi.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_system.h>

#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "activities/apps/netkit/NetKit.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {

GfxRenderer* gRenderer = nullptr;
std::string gDataDir;         // "/apps/data/<slug>" — set while an app is bound
bool gWifiBroughtUp = false;  // app called wifi_ensure(); tear down on unbind

int mapFont(const int32_t font) {
  switch (font) {
    case CP_FONT_SMALL:
      return SMALL_FONT_ID;
    case CP_FONT_UI_LARGE:
      return UI_12_FONT_ID;
    case CP_FONT_TITLE:
      return NOTOSANS_18_FONT_ID;
    case CP_FONT_UI:
    default:
      return UI_10_FONT_ID;
  }
}

// Joins the sandbox dir with a relative path; rejects escapes. Returns false
// when the path is unusable. `out` is a caller-provided stack buffer.
bool sandboxPath(const char* rel, char* out, const size_t cap) {
  if (gDataDir.empty() || rel == nullptr || rel[0] == '\0' || rel[0] == '/') return false;
  if (strstr(rel, "..") != nullptr) return false;
  const int n = snprintf(out, cap, "%s/%s", gDataDir.c_str(), rel);
  return n > 0 && static_cast<size_t>(n) < cap;
}

// ---- CpApi implementations ---------------------------------------------

uint32_t apiMillis() { return millis(); }
void apiDelayMs(const uint32_t ms) { delay(ms > 1000 ? 1000 : ms); }  // bound: keep the loop responsive
uint32_t apiRandom() { return esp_random(); }

void apiLog(const char* tag, const char* msg) {
  LOG_INF("DYNAPP", "[%s] %s", tag != nullptr ? tag : "app", msg != nullptr ? msg : "");
}

int32_t apiBattery() { return powerManager.getBatteryPercentage(); }
uint32_t apiFreeHeap() { return esp_get_free_heap_size(); }

int32_t apiScreenW() { return gRenderer != nullptr ? gRenderer->getScreenWidth() : 0; }
int32_t apiScreenH() { return gRenderer != nullptr ? gRenderer->getScreenHeight() : 0; }

void apiClear() {
  if (gRenderer != nullptr) gRenderer->clearScreen();
}

void apiPixel(const int32_t x, const int32_t y, const int32_t black) {
  if (gRenderer != nullptr) gRenderer->drawPixel(x, y, black != 0);
}

void apiLine(const int32_t x1, const int32_t y1, const int32_t x2, const int32_t y2, const int32_t black) {
  if (gRenderer != nullptr) gRenderer->drawLine(x1, y1, x2, y2, black != 0);
}

void apiRect(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const int32_t black) {
  if (gRenderer != nullptr) gRenderer->drawRect(x, y, w, h, black != 0);
}

void apiFillRect(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const int32_t black) {
  if (gRenderer != nullptr) gRenderer->fillRect(x, y, w, h, black != 0);
}

void apiText(const int32_t font, const int32_t x, const int32_t y, const char* utf8, const int32_t black,
             const int32_t style) {
  if (gRenderer == nullptr || utf8 == nullptr) return;
  gRenderer->drawText(mapFont(font), x, y, utf8, black != 0,
                      style == CP_TEXT_BOLD ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

int32_t apiTextWidth(const int32_t font, const char* utf8, const int32_t style) {
  if (gRenderer == nullptr || utf8 == nullptr) return 0;
  return gRenderer->getTextWidth(mapFont(font), utf8,
                                 style == CP_TEXT_BOLD ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

int32_t apiLineHeight(const int32_t font) { return gRenderer != nullptr ? gRenderer->getLineHeight(mapFont(font)) : 0; }

int32_t apiFileRead(const char* rel, void* buf, const uint32_t cap) {
  char path[128];
  if (!sandboxPath(rel, path, sizeof(path)) || buf == nullptr || cap == 0) return -1;
  HalFile f;
  if (!Storage.openFileForRead("DYNAPP", path, f)) return -1;
  return f.read(buf, cap);
}

int32_t apiFileWrite(const char* rel, const void* data, const uint32_t len) {
  char path[128];
  if (!sandboxPath(rel, path, sizeof(path)) || (data == nullptr && len > 0)) return -1;
  Storage.ensureDirectoryExists(gDataDir.c_str());
  HalFile f;
  if (!Storage.openFileForWrite("DYNAPP", path, f)) return -1;
  return f.write(data, len) == len ? 0 : -1;
}

int32_t apiFileDelete(const char* rel) {
  char path[128];
  if (!sandboxPath(rel, path, sizeof(path))) return -1;
  return Storage.remove(path) ? 0 : -1;
}

int32_t apiFileExists(const char* rel) {
  char path[128];
  if (!sandboxPath(rel, path, sizeof(path))) return 0;
  return Storage.exists(path) ? 1 : 0;
}

// ---- v1-appended: text helpers -----------------------------------------

void apiTextCentered(const int32_t font, const int32_t cx, const int32_t y, const char* utf8, const int32_t black,
                     const int32_t style) {
  if (gRenderer == nullptr || utf8 == nullptr) return;
  const auto epdStyle = style == CP_TEXT_BOLD ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int w = gRenderer->getTextWidth(mapFont(font), utf8, epdStyle);
  gRenderer->drawText(mapFont(font), cx - w / 2, y, utf8, black != 0, epdStyle);
}

void apiTextWrapped(const int32_t font, const int32_t x, const int32_t y, const int32_t width, const int32_t maxLines,
                    const char* utf8, const int32_t black, const int32_t style) {
  if (gRenderer == nullptr || utf8 == nullptr) return;
  UITheme::drawCenteredWrappedText(*gRenderer, Rect{x, y, width, gRenderer->getLineHeight(mapFont(font)) * maxLines},
                                   mapFont(font), utf8, maxLines, black != 0,
                                   style == CP_TEXT_BOLD ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR,
                                   UITheme::TextVerticalAlignment::TOP);
}

// ---- v1-appended: RTC ---------------------------------------------------

int32_t apiRtcNow(int32_t* year, int32_t* month, int32_t* day, int32_t* hour, int32_t* minute, int32_t* second,
                  int32_t* weekday) {
  if (!halClock.hasValidTime()) return 0;
  // Apply the configured local offset, then break down in UTC terms.
  const long offsetSec = (static_cast<long>(SETTINGS.clockUtcOffsetQ) - 48) * 15 * 60;
  const time_t local = halClock.nowUtc() + offsetSec;
  struct tm tmv;
  gmtime_r(&local, &tmv);
  if (year != nullptr) *year = tmv.tm_year + 1900;
  if (month != nullptr) *month = tmv.tm_mon + 1;
  if (day != nullptr) *day = tmv.tm_mday;
  if (hour != nullptr) *hour = tmv.tm_hour;
  if (minute != nullptr) *minute = tmv.tm_min;
  if (second != nullptr) *second = tmv.tm_sec;
  if (weekday != nullptr) *weekday = tmv.tm_wday;
  return 1;
}

// ---- v1-appended: networking -------------------------------------------

int32_t apiWifiConnected() { return netkit::wifiConnected() ? 1 : 0; }

int32_t apiWifiEnsure(const uint32_t timeoutMs) {
  if (netkit::wifiConnected()) return 1;
  const size_t count = WIFI_STORE.getCredentialCount();
  if (count == 0) return 0;
  // Try the last-connected network first, then the rest, sharing one budget.
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  WiFi.mode(WIFI_STA);
  const uint32_t deadline = millis() + timeoutMs;

  auto tryConnect = [&](const std::string& ssid, const std::string& pass) -> bool {
    if (ssid.empty()) return false;
    WiFi.begin(ssid.c_str(), pass.c_str());
    while (millis() < deadline) {
      if (WiFi.status() == WL_CONNECTED) {
        WIFI_STORE.setLastConnectedSsid(ssid);
        gWifiBroughtUp = true;
        return true;
      }
      delay(150);
    }
    return false;
  };

  if (!last.empty()) {
    if (const auto cred = WIFI_STORE.findCredential(last); cred) {
      if (tryConnect(cred->ssid, cred->password)) return 1;
    }
  }
  for (size_t i = 0; i < count && millis() < deadline; ++i) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == last) continue;
    if (tryConnect(cred->ssid, cred->password)) return 1;
  }
  return netkit::wifiConnected() ? 1 : 0;
}

int32_t apiHttpGet(const char* url, void* buf, const uint32_t capacity) {
  if (url == nullptr || buf == nullptr || capacity == 0) return CP_HTTP_ERR_ARGS;
  if (!netkit::wifiConnected()) return CP_HTTP_ERR_NO_WIFI;
  auto* out = static_cast<uint8_t*>(buf);
  uint32_t written = 0;
  bool overflow = false;
  const bool ok = HttpDownloader::fetchUrl(std::string(url), [&](const uint8_t* data, const size_t len) {
    if (written + len > capacity) {
      overflow = true;
      return false;
    }
    memcpy(out + written, data, len);
    written += len;
    return true;
  });
  if (overflow) return CP_HTTP_ERR_OVERFLOW;
  if (!ok) return CP_HTTP_ERR_TRANSPORT;
  return static_cast<int32_t>(written);
}

// Flash-resident, immutable table. Order MUST match CpApi exactly.
constexpr CpApi kApi = {
    CP_ABI_VERSION, sizeof(CpApi),   apiMillis,      apiDelayMs,    apiRandom,        apiLog,        apiBattery,
    apiFreeHeap,    apiScreenW,      apiScreenH,     apiClear,      apiPixel,         apiLine,       apiRect,
    apiFillRect,    apiText,         apiTextWidth,   apiLineHeight, apiFileRead,      apiFileWrite,  apiFileDelete,
    apiFileExists,  apiTextCentered, apiTextWrapped, apiRtcNow,     apiWifiConnected, apiWifiEnsure, apiHttpGet,
};

}  // namespace

namespace dynappapi {

void bind(GfxRenderer& renderer, const std::string& slug) {
  gRenderer = &renderer;
  gDataDir = "/apps/data/" + slug;
  gWifiBroughtUp = false;
}

void unbind() {
  // Power Wi-Fi down if this app brought it up, mirroring the network apps'
  // exit behavior so a dynamic app never leaves the radio on.
  if (gWifiBroughtUp) {
    netkit::teardownWifi();
    gWifiBroughtUp = false;
  }
  gRenderer = nullptr;
  gDataDir.clear();
}

const CpApi* table() { return &kApi; }

}  // namespace dynappapi

#endif  // !SIMULATOR
