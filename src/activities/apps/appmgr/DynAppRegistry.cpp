#include "DynAppRegistry.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "activities/apps/netkit/NetKit.h"
#include "network/HttpDownloader.h"
#include "util/FileOps.h"

namespace {

constexpr const char* kAppsDir = "/apps";
constexpr const char* kDataDir = "/apps/data";
constexpr const char* kCatalogUrlPath = "/apps/catalog.url";
constexpr const char* kCatalogCachePath = "/apps/catalog.json";
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
  int skipped = 0;
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
    // The same gate install/uninstall already apply. Without it the directory
    // listing is taken at face value, and a card written on macOS is full of
    // "._<name>.eapp" AppleDouble sidecars that list as broken apps.
    if (!validSlug(app.slug)) {
      ++skipped;
      continue;
    }
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
  if (skipped > 0) LOG_INF("DYNAPP", "ignored %d file(s) in /apps that are not valid app names", skipped);
  if (count == cap) LOG_ERR("DYNAPP", "installed-app scan hit the cap of %d; some apps are not listed", cap);
  return count;
}

int scanInstalledNames(InstalledAppName* out, const int cap) {
  int count = 0;
  int skipped = 0;
  auto dir = Storage.open(kAppsDir);
  if (!dir || !dir.isDirectory()) return 0;
  dir.rewindDirectory();
  for (auto f = dir.openNextFile(); f && count < cap; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    char name[48];
    if (!f.getName(name, sizeof(name))) continue;
    const size_t len = strlen(name);
    if (len < 6 || strcmp(name + len - 5, ".eapp") != 0) continue;
    InstalledAppName& app = out[count];
    memset(&app, 0, sizeof(app));
    const size_t stem = len - 5 < sizeof(app.slug) - 1 ? len - 5 : sizeof(app.slug) - 1;
    memcpy(app.slug, name, stem);
    app.slug[stem] = '\0';
    if (!validSlug(app.slug)) {  // see scanInstalled: macOS "._" sidecars, dotfiles
      ++skipped;
      continue;
    }
    copyStr(app.name, sizeof(app.name), app.slug);  // replaced below when the catalog knows better
    ++count;
  }
  if (skipped > 0) LOG_INF("DYNAPP", "ignored %d file(s) in /apps that are not valid app names", skipped);
  // Worth shouting about: the sort below runs after this loop, so hitting the
  // cap drops whichever apps the directory happened to yield last rather than
  // a predictable tail.
  if (count == cap) LOG_ERR("DYNAPP", "installed-app scan hit the cap of %d; some apps are not listed", cap);
  if (count == 0) return 0;

  // Slug order groups a family together (aibook/aichat/aidict/...) and, unlike
  // the FAT directory order, does not depend on install sequence.
  for (int i = 1; i < count; ++i) {
    InstalledAppName key = out[i];
    int j = i - 1;
    while (j >= 0 && strcmp(out[j].slug, key.slug) > 0) {
      out[j + 1] = out[j];
      --j;
    }
    out[j + 1] = key;
  }

  // One catalog parse for every name. The entry array is ~8KB, far too much
  // for this task's stack, and it is only needed for the length of this call.
  auto catalog = makeUniqueNoThrow<CatalogEntry[]>(kMaxCatalog);
  if (!catalog) {
    LOG_ERR("DYNAPP", "no heap for the catalog cache; app names fall back to slugs");
    return count;
  }
  const int catalogCount = loadCatalogCache(catalog.get(), kMaxCatalog);
  for (int i = 0; i < count; ++i) {
    const char* name = catalogNameFor(catalog.get(), catalogCount, out[i].slug);
    if (name != nullptr) copyStr(out[i].name, sizeof(out[i].name), name);
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

bool installFromFile(const std::string& srcPath, std::string& err) {
  const std::string leaf = FileOps::baseName(srcPath);
  if (leaf.size() < 6 || leaf.compare(leaf.size() - 5, 5, ".eapp") != 0) {
    err = "not an .eapp";
    return false;
  }
  const std::string slug = leaf.substr(0, leaf.size() - 5);
  if (!validSlug(slug.c_str())) {
    err = "bad name";
    return false;
  }
  Storage.ensureDirectoryExists(kAppsDir);
  const std::string finalPath = eappPath(slug.c_str());
  if (finalPath == srcPath) return true;  // already installed in place

  // Stage beside the target so a failed copy never clobbers a working install.
  const std::string tmpPath = finalPath + ".tmp";
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
  FileOps::CopyBuffer buf;
  if (!buf.valid() || !FileOps::copyFile(srcPath, tmpPath, buf)) {
    err = "copy failed";
    if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
    return false;
  }
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    err = "rename failed";
    Storage.remove(tmpPath.c_str());
    return false;
  }
  // A hand-installed image carries no version, so drop any stale tag rather
  // than let the catalog claim it is up to date.
  char verPath[80];
  snprintf(verPath, sizeof(verPath), "%s/%s.ver", kAppsDir, slug.c_str());
  if (Storage.exists(verPath)) Storage.remove(verPath);
  LOG_INF("APPREG", "installed %s from %s", slug.c_str(), srcPath.c_str());
  return true;
}

std::string defaultCatalogUrl() { return std::string(kDefaultCatalogUrl); }

bool setCatalogUrl(const std::string& url) {
  Storage.ensureDirectoryExists(kAppsDir);
  if (url.empty()) {  // clearing the override restores the built-in default
    if (Storage.exists(kCatalogUrlPath)) return Storage.remove(kCatalogUrlPath);
    return true;
  }
  if (url.compare(0, 4, "http") != 0) return false;
  HalFile f;
  if (!Storage.openFileForWrite("APPREG", kCatalogUrlPath, f)) return false;
  return f.write(url.data(), url.size()) == url.size();
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

namespace {

int parseCatalog(const std::string& body, CatalogEntry* out, const int cap) {
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return -1;
  int count = 0;
  for (JsonObjectConst item : doc["apps"].as<JsonArrayConst>()) {
    if (count >= cap) {
      LOG_ERR("DYNAPP", "catalog has more than %d entries; the rest are not shown", cap);
      break;
    }
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
  return count;
}

}  // namespace

int fetchCatalog(CatalogEntry* out, const int cap, std::string& err) {
  std::string body;
  if (!netkit::fetchToString(catalogUrl(), body, kMaxCatalogBytes)) {
    err = "download failed";
    return -1;
  }
  const int count = parseCatalog(body, out, cap);
  if (count < 0) {
    err = "bad json";
    return -1;
  }
  if (count == 0) {
    err = "empty catalog";
    return 0;
  }
  // Refresh the on-SD cache so installed-list names resolve offline. Best
  // effort: a failed write only costs the cache, never the fetch result.
  Storage.ensureDirectoryExists(kAppsDir);
  HalFile f;
  if (Storage.openFileForWrite("APPREG", kCatalogCachePath, f)) {
    f.write(body.data(), body.size());
  }
  return count;
}

int loadCatalogCache(CatalogEntry* out, const int cap) {
  if (!Storage.exists(kCatalogCachePath)) return 0;
  std::string body;
  body.resize(kMaxCatalogBytes);
  const size_t n = Storage.readFileToBuffer(kCatalogCachePath, body.data(), body.size());
  if (n == 0) return 0;
  body.resize(n);
  const int count = parseCatalog(body, out, cap);
  return count > 0 ? count : 0;
}

const char* catalogNameFor(const CatalogEntry* cat, const int count, const char* slug) {
  for (int i = 0; i < count; ++i) {
    if (strcmp(cat[i].slug, slug) == 0 && cat[i].name[0] != '\0') return cat[i].name;
  }
  return nullptr;
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
