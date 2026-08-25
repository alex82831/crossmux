#pragma once

#include <cstdint>
#include <string>

// On-SD registry for installable apps (see docs/engineering/dynapp.md).
//
//   /apps/<slug>.eapp       the app image (uploaded, downloaded, or copied)
//   /apps/<slug>.ver        installed version string (written by the catalog
//                           installer; uploads without it show "-")
//   /apps/data/<slug>/      the app's sandboxed data (survives updates)
//   /apps/catalog.url       optional one-line override of the catalog URL
//
// The catalog is a JSON document: {"apps":[{"slug","name","version","bytes",
// "url","note"}]}. The default URL points at this repository's store/
// directory so a stock build has a working install source.
namespace dynappreg {

constexpr int kMaxApps = 24;
constexpr int kMaxCatalog = 24;

struct InstalledApp {
  char slug[32];
  char version[16];  // "-" when unknown
  uint32_t eappBytes;
  uint32_t dataBytes;
};

struct CatalogEntry {
  char slug[32];
  char name[48];
  char version[16];
  char note[64];
  char url[160];
  uint32_t bytes;
};

// ---- Installed apps ----------------------------------------------------
int scanInstalled(InstalledApp* out, int cap);
std::string eappPath(const char* slug);
bool uninstall(const char* slug);  // removes image, version tag and data
bool clearData(const char* slug);  // removes only /apps/data/<slug>/
const char* installedVersion(const InstalledApp* apps, int count, const char* slug);

// ---- Online catalog ----------------------------------------------------
std::string catalogUrl();
// Fetches and parses the catalog. Returns entry count, or -1 with `err` set.
int fetchCatalog(CatalogEntry* out, int cap, std::string& err);
// Downloads entry.url to /apps/<slug>.eapp (staged via a .tmp so a failed
// download never clobbers a working install), then records the version.
bool installFromCatalog(const CatalogEntry& entry, std::string& err);

}  // namespace dynappreg
