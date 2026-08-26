#include "AppManagerActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/apps/filemgr/FileManagerActivity.h"
#include "activities/apps/netkit/NetKit.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/AppVisibilitySettingsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

void formatKb(char* out, const size_t cap, const uint32_t bytes) {
  if (bytes < 1024) {
    snprintf(out, cap, "%u B", static_cast<unsigned>(bytes));
  } else {
    snprintf(out, cap, "%u.%u KB", static_cast<unsigned>(bytes / 1024), static_cast<unsigned>((bytes % 1024) / 103));
  }
}

}  // namespace

void AppManagerActivity::onEnter() {
  UiListActivity::onEnter();
  mode_ = Mode::Installed;
  // The cached catalog (shipped in the flash package, refreshed on every
  // successful fetch) supplies display names for installed apps offline.
  catalogCount_ = dynappreg::loadCatalogCache(catalog_, dynappreg::kMaxCatalog);
  rebuildInstalledRows();
  requestUpdate();
}

void AppManagerActivity::onExit() { UiListActivity::onExit(); }

const char* AppManagerActivity::headerTitle() const {
  return mode_ == Mode::Installed ? tr(STR_APPMGR_TITLE) : tr(STR_APPMGR_CATALOG);
}

void AppManagerActivity::rebuildInstalledRows() {
  installedCount_ = dynappreg::scanInstalled(installed_, dynappreg::kMaxApps);
  int row = 0;
  char buf[96];

  rowTitles_[row] = tr(STR_APPMGR_INSTALLED);
  rowItems_[row] = {};
  rowItems_[row].label = rowTitles_[row].c_str();
  rowItems_[row].isHeader = true;
  ++row;

  if (installedCount_ == 0) {
    rowTitles_[row] = tr(STR_APPMGR_NONE_INSTALLED);
    rowItems_[row] = {};
    rowItems_[row].label = rowTitles_[row].c_str();
    rowItems_[row].enabled = false;
    ++row;
  }
  for (int i = 0; i < installedCount_ && row < kMaxRows; ++i, ++row) {
    // Prefer the catalog display name (中文); manually copied files still
    // resolve because the flash package ships /apps/catalog.json.
    const char* name = dynappreg::catalogNameFor(catalog_, catalogCount_, installed_[i].slug);
    rowTitles_[row] = name != nullptr ? name : installed_[i].slug;
    char size[24], data[24];
    formatKb(size, sizeof(size), installed_[i].eappBytes);
    formatKb(data, sizeof(data), installed_[i].dataBytes);
    const char* verPrefix = "";
    char ver[20] = "";
    if (installed_[i].version[0] != '-' || installed_[i].version[1] != '\0') {
      snprintf(ver, sizeof(ver), "v%s · ", installed_[i].version);
      verPrefix = ver;
    }
    if (name != nullptr) {  // keep the file identity visible once renamed
      snprintf(buf, sizeof(buf), "%s · %s%s · %s %s", installed_[i].slug, verPrefix, size, tr(STR_APPMGR_DATA), data);
    } else {
      snprintf(buf, sizeof(buf), "%s%s · %s %s", verPrefix, size, tr(STR_APPMGR_DATA), data);
    }
    rowSubtitles_[row] = buf;
    rowItems_[row] = {};
    rowItems_[row].label = rowTitles_[row].c_str();
    rowItems_[row].subtitle = rowSubtitles_[row].c_str();
    rowItems_[row].actionValue = static_cast<int16_t>(i);
  }

  rowTitles_[row] = tr(STR_APPMGR_GET_APPS);
  rowItems_[row] = {};
  rowItems_[row].label = rowTitles_[row].c_str();
  rowItems_[row].isHeader = true;
  ++row;

  rowTitles_[row] = tr(STR_APPMGR_CATALOG);
  rowSubtitles_[row] = tr(STR_APPMGR_CATALOG_SUB);
  rowItems_[row] = {};
  rowItems_[row].label = rowTitles_[row].c_str();
  rowItems_[row].subtitle = rowSubtitles_[row].c_str();
  rowItems_[row].actionValue = kActionCatalog;
  ++row;

  rowTitles_[row] = tr(STR_APPMGR_INSTALL_FROM_DISK);
  rowSubtitles_[row] = tr(STR_APPMGR_INSTALL_FROM_DISK_SUB);
  rowItems_[row] = {};
  rowItems_[row].label = rowTitles_[row].c_str();
  rowItems_[row].subtitle = rowSubtitles_[row].c_str();
  rowItems_[row].actionValue = kActionInstallFromDisk;
  ++row;

  rowTitles_[row] = tr(STR_APPMGR_BROWSER_INSTALL);
  rowSubtitles_[row] = tr(STR_APPMGR_BROWSER_INSTALL_SUB);
  rowItems_[row] = {};
  rowItems_[row].label = rowTitles_[row].c_str();
  rowItems_[row].subtitle = rowSubtitles_[row].c_str();
  rowItems_[row].actionValue = kActionBrowserHint;
  ++row;

  // The stored override, or the built-in default when none is set.
  rowTitles_[row] = tr(STR_APPMGR_CATALOG_SERVER);
  rowSubtitles_[row] = dynappreg::catalogUrl();
  rowItems_[row] = {};
  rowItems_[row].label = rowTitles_[row].c_str();
  rowItems_[row].subtitle = rowSubtitles_[row].c_str();
  rowItems_[row].actionValue = kActionCatalogServer;
  ++row;

  rowTitles_[row] = tr(STR_APPMGR_BUILTIN_VISIBILITY);
  rowSubtitles_[row] = tr(STR_APPMGR_BUILTIN_VISIBILITY_SUB);
  rowItems_[row] = {};
  rowItems_[row].label = rowTitles_[row].c_str();
  rowItems_[row].subtitle = rowSubtitles_[row].c_str();
  rowItems_[row].actionValue = kActionBuiltinVisibility;
  ++row;

  rowCount_ = row;
  nav.reset();
}

void AppManagerActivity::rebuildCatalogRows() {
  int row = 0;
  char buf[96];
  if (catalogCount_ <= 0) {
    rowTitles_[row] = catalogError_.empty() ? tr(STR_APPMGR_CATALOG_FAILED)
                                            : std::string(tr(STR_APPMGR_CATALOG_FAILED)) + ": " + catalogError_;
    rowItems_[row] = {};
    rowItems_[row].label = rowTitles_[row].c_str();
    rowItems_[row].enabled = false;
    rowCount_ = 1;
    nav.reset();
    return;
  }
  if (!catalogError_.empty()) {  // cached entries after a failed fetch
    rowTitles_[row] = tr(STR_APPMGR_CATALOG_CACHED);
    rowItems_[row] = {};
    rowItems_[row].label = rowTitles_[row].c_str();
    rowItems_[row].enabled = false;
    ++row;
  }
  for (int i = 0; i < catalogCount_ && row < kMaxRows; ++i, ++row) {
    rowTitles_[row] = catalog_[i].name;
    const char* installedVer = dynappreg::installedVersion(installed_, installedCount_, catalog_[i].slug);
    char size[24];
    formatKb(size, sizeof(size), catalog_[i].bytes);
    const char* status = installedVer == nullptr                          ? tr(STR_APPMGR_NOT_INSTALLED)
                         : strcmp(installedVer, catalog_[i].version) == 0 ? tr(STR_APPMGR_UP_TO_DATE)
                                                                          : tr(STR_APPMGR_UPDATABLE);
    if (catalog_[i].note[0] != '\0') {
      snprintf(buf, sizeof(buf), "v%s · %s · %s · %s", catalog_[i].version, size, status, catalog_[i].note);
    } else {
      snprintf(buf, sizeof(buf), "v%s · %s · %s", catalog_[i].version, size, status);
    }
    rowSubtitles_[row] = buf;
    rowItems_[row] = {};
    rowItems_[row].label = rowTitles_[row].c_str();
    rowItems_[row].subtitle = rowSubtitles_[row].c_str();
    rowItems_[row].actionValue = static_cast<int16_t>(i);
  }
  rowCount_ = row;
  nav.reset();
}

void AppManagerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(rowCount_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, true);
  screen.list(props);
}

void AppManagerActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount_ || rowItems_[index].isHeader || !rowItems_[index].enabled) return;
  const int action = rowItems_[index].actionValue;

  if (mode_ == Mode::Catalog) {
    installEntry(action);
    return;
  }
  switch (action) {
    case kActionCatalog:
      openCatalog();
      return;
    case kActionBrowserHint:
      hintVisible_ = true;
      requestUpdate();
      return;
    case kActionInstallFromDisk:
      pickFromDisk();
      return;
    case kActionCatalogServer:
      editCatalogServer();
      return;
    case kActionBuiltinVisibility:
      startActivityForResultWith<AppVisibilitySettingsActivity>([this](const ActivityResult&) {
        rebuildInstalledRows();
        requestUpdate();
      });
      return;
    default:
      // Confirm runs the app. Opening a menu here made launching cost two
      // presses, which is the wrong default: running is what a row is for, and
      // uninstall/clear-data belong on the long press like everywhere else.
      if (action >= 0 && action < installedCount_) runApp(installed_[action].slug);
      return;
  }
}

// The installed-app index behind a row, or -1 when the row is a header, a
// disabled placeholder, or one of the kAction* commands.
int AppManagerActivity::installedIndexForRow(const int index) const {
  if (mode_ != Mode::Installed) return -1;
  if (index < 0 || index >= rowCount_ || rowItems_[index].isHeader || !rowItems_[index].enabled) return -1;
  const int action = rowItems_[index].actionValue;
  return action >= 0 && action < installedCount_ ? action : -1;
}

int AppManagerActivity::selectedInstalledIndex() { return installedIndexForRow(activeNav().selected); }

void AppManagerActivity::onRowLongPress(const int index) {
  const int installed = installedIndexForRow(index);
  if (installed >= 0) showAppActions(installed);
}

void AppManagerActivity::showAppActions(const int installedIndex) {
  const char* actions[] = {tr(STR_APPMGR_RUN), tr(STR_APPMGR_UNINSTALL), tr(STR_APPMGR_CLEAR_DATA), tr(STR_CANCEL)};
  const std::string slug = installed_[installedIndex].slug;
  const char* name = dynappreg::catalogNameFor(catalog_, catalogCount_, slug.c_str());
  optionPopup_.show(name != nullptr ? name : slug.c_str(), actions, 4, 0, [this, slug](const int choice) {
    switch (choice) {
      case 0:
        runApp(slug.c_str());
        break;
      case 1:
        dynappreg::uninstall(slug.c_str());
        rebuildInstalledRows();
        break;
      case 2:
        dynappreg::clearData(slug.c_str());
        rebuildInstalledRows();
        break;
      default:
        break;
    }
    requestUpdate();
  });
  requestUpdate();
}

void AppManagerActivity::runApp(const char* slug) {
#ifndef SIMULATOR
  activityManager.startDynApp(dynappreg::eappPath(slug), slug);
#else
  (void)slug;
#endif
}

void AppManagerActivity::showBlockingStatus(const char* line1, const char* line2) {
  char msg[128];
  if (line2 != nullptr && line2[0] != '\0') {
    snprintf(msg, sizeof(msg), "%s\n%s", line1, line2);
  } else {
    snprintf(msg, sizeof(msg), "%s", line1);
  }
  renderer.clearScreen();
  UITheme::drawCenteredWrappedText(renderer, Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                                   UI_12_FONT_ID, msg, 3);
  renderer.displayBuffer();
}

void AppManagerActivity::openCatalog() {
  const auto fetchAndShow = [this] {
    showBlockingStatus(tr(STR_APPMGR_FETCHING), nullptr);
    catalogError_.clear();
    catalogCount_ = dynappreg::fetchCatalog(catalog_, dynappreg::kMaxCatalog, catalogError_);
    if (catalogCount_ <= 0) {
      // Offline fallback: browse the cached catalog (installs still need the
      // network). catalogError_ stays set so the list leads with a notice.
      const int cached = dynappreg::loadCatalogCache(catalog_, dynappreg::kMaxCatalog);
      if (cached > 0) catalogCount_ = cached;
    }
    mode_ = Mode::Catalog;
    rebuildCatalogRows();
    requestUpdate();
  };
  if (netkit::wifiConnected()) {
    fetchAndShow();
    return;
  }
  startActivityForResultWith<WifiSelectionActivity>(
      [this, fetchAndShow](const ActivityResult& result) {
        if (!result.isCancelled && netkit::wifiConnected()) {
          fetchAndShow();
        }
      },
      true);
}

void AppManagerActivity::installEntry(const int catalogIndex) {
  if (catalogIndex < 0 || catalogIndex >= catalogCount_) return;
  const dynappreg::CatalogEntry& entry = catalog_[catalogIndex];
  showBlockingStatus(tr(STR_APPMGR_INSTALLING), entry.name);
  std::string err;
  const bool ok = dynappreg::installFromCatalog(entry, err);
  // Refresh install state so the row's status flips immediately.
  installedCount_ = dynappreg::scanInstalled(installed_, dynappreg::kMaxApps);
  rebuildCatalogRows();
  showBlockingStatus(ok ? tr(STR_APPMGR_INSTALL_OK) : tr(STR_APPMGR_INSTALL_FAILED), entry.name);
  delay(900);  // let the verdict register before the list repaints
  requestUpdate();
}

void AppManagerActivity::pickFromDisk() {
  // Reuse the File Manager as a picker rather than growing a second browser.
  startActivityForResultWith<FileManagerActivity>(
      [this](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<FilePathResult>(result.data)) return;
        const std::string& path = std::get<FilePathResult>(result.data).path;
        showBlockingStatus(tr(STR_APPMGR_INSTALLING), path.c_str());
        std::string err;
        const bool ok = dynappreg::installFromFile(path, err);
        if (!ok) LOG_ERR("AppMgr", "install %s failed: %s", path.c_str(), err.c_str());
        rebuildInstalledRows();
        showBlockingStatus(ok ? tr(STR_APPMGR_INSTALL_OK) : tr(STR_APPMGR_INSTALL_FAILED), path.c_str());
        delay(900);
        requestUpdate();
      },
      FileManagerActivity::Mode::PickEapp, std::string("/"));
}

void AppManagerActivity::editCatalogServer() {
  const std::string current = dynappreg::catalogUrl();
  startActivityForResultWith<KeyboardEntryActivity>(
      [this](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        std::string url = std::get<KeyboardResult>(result.data).text;
        // Trim, then treat an emptied field as "restore the default".
        while (!url.empty() && (url.back() == ' ' || url.back() == '\r' || url.back() == '\n')) url.pop_back();
        const bool cleared = url.empty();
        if (!dynappreg::setCatalogUrl(url)) {
          showBlockingStatus(tr(STR_APPMGR_URL_INVALID), nullptr);
          delay(900);
        } else {
          showBlockingStatus(cleared ? tr(STR_APPMGR_URL_RESET) : tr(STR_APPMGR_URL_SAVED), nullptr);
          delay(700);
        }
        rebuildInstalledRows();
        requestUpdate();
      },
      tr(STR_APPMGR_CATALOG_SERVER), current, size_t{180}, InputType::Url);
}

void AppManagerActivity::onBackButton() {
  if (mode_ == Mode::Catalog) {
    mode_ = Mode::Installed;
    rebuildInstalledRows();
    requestUpdate();
    return;
  }
  activityManager.goToApps();
}

void AppManagerActivity::render(RenderLock&& lock) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

bool AppManagerActivity::handleCustomInput() {
  if (hintVisible_) {
    int tapX = 0, tapY = 0;
    if (mappedInput.wasAnyReleased() || mappedInput.wasScreenTapped(tapX, tapY)) {
      hintVisible_ = false;
      requestUpdate();
    }
    return true;
  }
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  // Confirm runs the app, so uninstall / clear-data live behind Left as well
  // as the touch long-press — a button-only user must still reach them. Only
  // consumed on a row that actually has something to manage.
  if (mappedInput.wasReleased(MappedInputManager::Button::ScreenLeft)) {
    const int installed = selectedInstalledIndex();
    if (installed >= 0) {
      showAppActions(installed);
      return true;
    }
  }
  return false;
}

void AppManagerActivity::drawFooter() {
  const bool manageable = mode_ == Mode::Installed && selectedInstalledIndex() >= 0;
  // Same reason as FileManager: the label and the handler must go through the
  // same orientation transform, which mapLabels' previous/next slots do not.
  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), manageable ? tr(STR_APPMGR_RUN) : tr(STR_SELECT),
                                                       manageable ? tr(STR_APPMGR_MANAGE) : "", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (hintVisible_) {
    // Browser-install instructions overlay (same visual family as AppAbout).
    const int sw = renderer.getScreenWidth();
    const int sh = renderer.getScreenHeight();
    const int w = sw * 4 / 5;
    const int h = sh / 3;
    const int x = (sw - w) / 2;
    const int y = (sh - h) / 2;
    renderer.fillRect(x - 2, y - 2, w + 4, h + 4, true);
    renderer.fillRect(x, y, w, h, false);
    renderer.drawRect(x + 3, y + 3, w - 6, h - 6, true);
    UITheme::drawCenteredWrappedText(renderer, Rect{x + 14, y + 12, w - 28, h - 24}, UI_10_FONT_ID,
                                     tr(STR_APPMGR_BROWSER_HINT), 8, true, EpdFontFamily::REGULAR,
                                     UITheme::TextVerticalAlignment::TOP);
  }
}
