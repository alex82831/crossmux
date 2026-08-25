#include "DynAppRegistry.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "activities/apps/netkit/NetKit.h"
#include "network/HttpDownloader.h"

namespace {

constexpr const char* kAppsDir = "/apps";
constexpr const char* kDataDir = "/apps/data";
constexpr const char* kCatalogUrlPath = "/apps/catalog.url";
// Served straight from this repository, so a stock build has a live install
// source with no extra infrastructure. Override with /apps/catalog.url.
constexpr const char* kDefaultCatalogUrl =
    "https://raw.githubusercontent.com/alex82831/crossmux/claude/project-research-firmware-build-xb63d3/store/"
    "catalog.json";
constexpr uint32_t kMaxCatalogBytes = 16 * 1024;  // catalog JSON cap

bool validSlug(const char* slug) {
  if (slug == nullptr || slug[0] == '\0') return false;
  for (const char* p = slug; *p; ++p) {
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok || static_cast<size_t>(p - slug) >= 31) return false;
  }
  return true;
}

void copyStr(char* dst, const size_t cap, const char* src) { snprintf(dst, cap, "%s", src != nullptr ? src : ""); }

uint32_t dirBytes(const char* path) {
  uint32_t total = 0;
  auto dir = Storage.open(path);
  if (dir && dir.isDirectory()) {
    dir.rewindDirectory();
    for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (!f.isDirectory()) total += f.size();
    }
  }
  return total;
}

}  // namespace

namespace dynappreg {

int scanInstalled(InstalledApp* out, const int cap) {
  int count = 0;
  auto dir = Storage.open(kAppsDir);
  if (!dir || !dir.isDirectory()) return 0;
  dir.rewindDirectory();
  for (auto f = dir.openNextFile(); f && count < cap; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    char name[48];
    if (!f.getName(name, sizeof(name))) continue;
    const size_t len = strlen(name);
    if (len < 6 || strcmp(name + len - 5, ".eapp") != 0) continue;
    InstalledApp& app = out[count];
    memset(&app, 0, sizeof(app));
    const size_t stem = len - 5 < sizeof(app.slug) - 1 ? len - 5 : sizeof(app.slug) - 1;
    memcpy(app.slug, name, stem);
    app.slug[stem] = '\0';
    app.eappBytes = f.size();
    // Version tag, if the installer wrote one.
    char verPath[64];
    snprintf(verPath, sizeof(verPath), "%s/%s.ver", kAppsDir, app.slug);
    copyStr(app.version, sizeof(app.version), "-");
    const size_t n = Storage.readFileToBuffer(verPath, app.version, sizeof(app.version));
    if (n == 0) copyStr(app.version, sizeof(app.version), "-");
    for (char* p = app.version; *p; ++p) {
      if (*p == '\r' || *p == '\n') *p = '\0';
    }
    char dataPath[80];
    snprintf(dataPath, sizeof(dataPath), "%s/%s", kDataDir, app.slug);
    app.dataBytes = dirBytes(dataPath);
    ++count;
  }
  return count;
}

std::string eappPath(const char* slug) { return std::string(kAppsDir) + "/" + slug + ".eapp"; }

bool uninstall(const char* slug) {
  if (!validSlug(slug)) return false;
  char path[80];
  snprintf(path, sizeof(path), "%s/%s.eapp", kAppsDir, slug);
  const bool removed = Storage.remove(path);
  snprintf(path, sizeof(path), "%s/%s.ver", kAppsDir, slug);
  if (Storage.exists(path)) Storage.remove(path);
  clearData(slug);
  return removed;
}

bool clearData(const char* slug) {
  if (!validSlug(slug)) return false;
  char path[80];
  snprintf(path, sizeof(path), "%s/%s", kDataDir, slug);
  if (!Storage.exists(path)) return true;
  return Storage.removeDir(path);
}

const char* installedVersion(const InstalledApp* apps, const int count, const char* slug) {
  for (int i = 0; i < count; ++i) {
    if (strcmp(apps[i].slug, slug) == 0) return apps[i].version;
  }
  return nullptr;
}

std::string catalogUrl() {
  // One-line override file, editable through the web file manager.
  char buf[192] = {};
  const size_t n = Storage.readFileToBuffer(kCatalogUrlPath, buf, sizeof(buf));
  if (n > 8) {  // anything shorter cannot be a URL
    for (char* p = buf; *p; ++p) {
      if (*p == '\r' || *p == '\n' || *p == ' ') {
        *p = '\0';
        break;
      }
    }
    if (strncmp(buf, "http", 4) == 0) return std::string(buf);
  }
  return std::string(kDefaultCatalogUrl);
}

int fetchCatalog(CatalogEntry* out, const int cap, std::string& err) {
  std::string body;
  if (!netkit::fetchToString(catalogUrl(), body, kMaxCatalogBytes)) {
    err = "download failed";
    return -1;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    err = "bad json";
    return -1;
  }
  int count = 0;
  for (JsonObjectConst item : doc["apps"].as<JsonArrayConst>()) {
    if (count >= cap) break;
    CatalogEntry& e = out[count];
    memset(&e, 0, sizeof(e));
    copyStr(e.slug, sizeof(e.slug), item["slug"] | "");
    if (!validSlug(e.slug)) continue;
    copyStr(e.name, sizeof(e.name), item["name"] | e.slug);
    copyStr(e.version, sizeof(e.version), item["version"] | "?");
    copyStr(e.note, sizeof(e.note), item["note"] | "");
    copyStr(e.url, sizeof(e.url), item["url"] | "");
    e.bytes = item["bytes"] | 0;
    if (e.url[0] == '\0') continue;
    ++count;
  }
  if (count == 0) err = "empty catalog";
  return count;
}

bool installFromCatalog(const CatalogEntry& entry, std::string& err) {
  if (!validSlug(entry.slug)) {
    err = "bad slug";
    return false;
  }
  Storage.ensureDirectoryExists(kAppsDir);
  const std::string finalPath = eappPath(entry.slug);
  const std::string tmpPath = finalPath + ".tmp";
  const auto result = HttpDownloader::downloadToFile(entry.url, tmpPath);
  if (result != HttpDownloader::OK) {
    err = "download failed";
    if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
    return false;
  }
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    err = "rename failed";
    return false;
  }
  char verPath[80];
  snprintf(verPath, sizeof(verPath), "%s/%s.ver", kAppsDir, entry.slug);
  HalFile f;
  if (Storage.openFileForWrite("APPREG", verPath, f)) {
    f.write(entry.version, strlen(entry.version));
  }
  LOG_INF("APPREG", "installed %s v%s", entry.slug, entry.version);
  return true;
}

}  // namespace dynappreg
