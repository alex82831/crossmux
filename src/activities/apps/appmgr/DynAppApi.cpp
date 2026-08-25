#ifndef SIMULATOR

#include "DynAppApi.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_random.h>
#include <esp_system.h>

#include <cstring>

#include <HalPowerManager.h>
#include "fontIds.h"

namespace {

GfxRenderer* gRenderer = nullptr;
std::string gDataDir;  // "/apps/data/<slug>" — set while an app is bound

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

int32_t apiLineHeight(const int32_t font) {
  return gRenderer != nullptr ? gRenderer->getLineHeight(mapFont(font)) : 0;
}

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

// Flash-resident, immutable table. Order MUST match CpApi exactly.
constexpr CpApi kApi = {
    CP_ABI_VERSION,
    sizeof(CpApi),
    apiMillis,
    apiDelayMs,
    apiRandom,
    apiLog,
    apiBattery,
    apiFreeHeap,
    apiScreenW,
    apiScreenH,
    apiClear,
    apiPixel,
    apiLine,
    apiRect,
    apiFillRect,
    apiText,
    apiTextWidth,
    apiLineHeight,
    apiFileRead,
    apiFileWrite,
    apiFileDelete,
    apiFileExists,
};

}  // namespace

namespace dynappapi {

void bind(GfxRenderer& renderer, const std::string& slug) {
  gRenderer = &renderer;
  gDataDir = "/apps/data/" + slug;
}

void unbind() {
  gRenderer = nullptr;
  gDataDir.clear();
}


const CpApi* table() { return &kApi; }

}  // namespace dynappapi

#endif  // !SIMULATOR
