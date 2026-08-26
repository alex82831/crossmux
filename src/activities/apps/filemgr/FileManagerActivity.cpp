#include "FileManagerActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/apps/appmgr/DynAppRegistry.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/FileOps.h"

namespace fui = freeink::ui;

namespace {

constexpr size_t kMaxNameBytes = 64;  // keyboard entry cap for names

// Longest path component the SD layer accepts in one name buffer.
constexpr size_t kNameBuf = 160;

bool isEappName(const std::string& name) { return FsHelpers::checkFileExtension(name, ".eapp"); }

bool isReadableBook(const std::string& name) {
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
         FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name);
}

}  // namespace

void FileManagerActivity::onEnter() {
  UiListActivity::onEnter();
  showHidden_ = SETTINGS.showHiddenFiles;
  {
    RenderLock lock(*this);
    loadEntries();
  }
  requestUpdate();
}

void FileManagerActivity::onExit() { UiListActivity::onExit(); }

const char* FileManagerActivity::headerTitle() const {
  return mode_ == Mode::PickEapp ? tr(STR_FILEMGR_PICK_EAPP) : tr(STR_FILEMGR_TITLE);
}

std::string FileManagerActivity::pathFor(const Entry& entry) const { return FileOps::joinPath(path_, entry.name); }

// ---- listing -------------------------------------------------------------

void FileManagerActivity::loadEntries() {
  entries_.clear();

  auto dir = Storage.open(path_.c_str());
  if (!dir || !dir.isDirectory()) {
    rebuildRows();
    return;
  }
  dir.rewindDirectory();

  char name[kNameBuf];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.getName(name, sizeof(name)) == 0) continue;
    if (!showHidden_ && name[0] == '.') continue;
    if (strcmp(name, "System Volume Information") == 0) continue;

    Entry entry;
    entry.name = name;
    entry.isDir = file.isDirectory();
    // Directory sizes need a full walk; skip it here so opening a folder stays
    // instant, and report it on demand from the item menu instead.
    entry.size = entry.isDir ? 0 : file.fileSize64();
    entries_.push_back(std::move(entry));
  }
  dir.close();

  // Directories first, then files, each in natural order.
  std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
    if (a.isDir != b.isDir) return a.isDir;
    return FsHelpers::naturalLess(a.name, b.name);
  });
  rebuildRows();
}

void FileManagerActivity::rebuildRows() {
  const size_t rows = entries_.size() + (hasParentRow() ? 1 : 0);
  rowLabels_.clear();
  rowSubs_.clear();
  rowItems_.clear();
  rowLabels_.reserve(rows);
  rowSubs_.reserve(rows);
  rowItems_.reserve(rows);

  if (hasParentRow()) {
    rowLabels_.emplace_back(tr(STR_FILEMGR_UP));
    rowSubs_.emplace_back(path_);
  }
  char size[24];
  for (const auto& e : entries_) {
    rowLabels_.push_back(e.name);
    if (e.isDir) {
      rowSubs_.emplace_back(tr(STR_FILEMGR_FOLDER));
    } else {
      FileOps::formatSize(e.size, size, sizeof(size));
      rowSubs_.emplace_back(size);
    }
  }
  // Pointers are only stable once both string vectors have stopped growing.
  for (size_t i = 0; i < rowLabels_.size(); ++i) {
    fui::ListItem item{};
    item.label = rowLabels_[i].c_str();
    item.subtitle = rowSubs_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    const bool isParent = hasParentRow() && i == 0;
    UIIcon icon = UIIcon::Folder;
    if (!isParent) {
      const Entry& entry = entries_[entryIndexFor(static_cast<int>(i))];
      icon = entry.isDir              ? UIIcon::Folder
             : isEappName(entry.name) ? UIIcon::AppStore
                                      : UITheme::getFileIcon(entry.name);
    }
    item.icon = listIconFor(icon);
    rowItems_.push_back(item);
  }
}

void FileManagerActivity::navigateTo(const std::string& path, const int selectIndex) {
  {
    RenderLock lock(*this);
    notice_.clear();  // callers set a fresh one after, if they have news
    path_ = path.empty() ? "/" : path;
    loadEntries();
    // A delete can shrink the list under the caller's remembered index.
    const int last = listCount() - 1;
    nav.selected = last < 0 ? 0 : std::clamp(selectIndex, 0, last);
    nav.top = 0;
  }
  requestUpdate();
}

void FileManagerActivity::goUp() {
  if (path_ == "/") {
    // As a picker we were pushed onto the stack: pop back to the caller with
    // no FilePathResult (it reads that as cancelled). Going to the Apps menu
    // here would tear the whole stack down instead of returning.
    if (mode_ == Mode::PickEapp) {
      finish();
      return;
    }
    activityManager.goToApps();
    return;
  }
  const std::string child = FileOps::baseName(path_);
  const std::string parent = FileOps::parentPath(path_);
  {
    RenderLock lock(*this);
    path_ = parent;
    loadEntries();
    // Land the cursor on the directory we came out of.
    nav.selected = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].name == child) {
        nav.selected = static_cast<int>(i) + (hasParentRow() ? 1 : 0);
        break;
      }
    }
    nav.top = 0;
  }
  requestUpdate();
}

// ---- rendering -----------------------------------------------------------

void FileManagerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, true);
  screen.list(props);
}

void FileManagerActivity::drawFooter() {
  const bool pick = mode_ == Mode::PickEapp;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), pick ? "" : tr(STR_FILEMGR_DIR_MENU), "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (!notice_.empty()) {
    const int sw = renderer.getScreenWidth();
    const int sh = renderer.getScreenHeight();
    const int h = renderer.getLineHeight(UI_12_FONT_ID) + 16;
    const int y = sh - h - 34;
    renderer.fillRect(0, y, sw, h, false);
    renderer.drawRect(8, y, sw - 16, h, true);
    UITheme::drawCenteredText(renderer, Rect{0, 0, sw, sh}, UI_12_FONT_ID, y + 8, notice_.c_str());
  }
}

void FileManagerActivity::showNotice(const char* message) {
  notice_ = message != nullptr ? message : "";
  requestUpdate();
}

void FileManagerActivity::showBlocking(const char* message, const char* detail) {
  char msg[160];
  if (detail != nullptr && detail[0] != '\0') {
    snprintf(msg, sizeof(msg), "%s\n%s", message, detail);
  } else {
    snprintf(msg, sizeof(msg), "%s", message);
  }
  renderer.clearScreen();
  UITheme::drawCenteredWrappedText(renderer, Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                                   UI_12_FONT_ID, msg, 3);
  renderer.displayBuffer();
}

// ---- input ---------------------------------------------------------------

void FileManagerActivity::onBackButton() { goUp(); }

bool FileManagerActivity::handleCustomInput() {
  if (popup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  // Left opens the directory menu; the picker keeps its input surface minimal.
  if (mode_ == Mode::Manage && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    showDirMenu();
    return true;
  }
  return false;
}

void FileManagerActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (hasParentRow() && index == 0) {
    goUp();
    return;
  }
  const int entryIndex = entryIndexFor(index);
  if (entryIndex < 0 || entryIndex >= static_cast<int>(entries_.size())) return;
  const Entry& entry = entries_[entryIndex];

  if (entry.isDir) {
    navigateTo(pathFor(entry));
    return;
  }
  if (mode_ == Mode::PickEapp) {
    if (!isEappName(entry.name)) {
      showNotice(tr(STR_FILEMGR_NOT_EAPP));
      return;
    }
    ActivityResult res{FilePathResult{pathFor(entry)}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }
  openEntry(entry);
}

void FileManagerActivity::onRowLongPress(const int index) {
  if (mode_ != Mode::Manage) return;
  if (index < 0 || index >= listCount()) return;
  if (hasParentRow() && index == 0) return;  // ".." has no operations
  app.clearTapFlash();
  showItemMenu(entryIndexFor(index));
}

// ---- open dispatch -------------------------------------------------------

void FileManagerActivity::openEntry(const Entry& entry) {
  const std::string full = pathFor(entry);
  if (isEappName(entry.name)) {
    installEapp(full);
    return;
  }
  if (isReadableBook(entry.name)) {
    onSelectBook(full);
    return;
  }
  showNotice(tr(STR_FILEMGR_NO_HANDLER));
}

void FileManagerActivity::installEapp(const std::string& fullPath) {
  showBlocking(tr(STR_FILEMGR_INSTALLING), FileOps::baseName(fullPath).c_str());
  std::string err;
  const bool ok = dynappreg::installFromFile(fullPath, err);
  showNotice(ok ? tr(STR_FILEMGR_INSTALL_OK) : tr(STR_FILEMGR_INSTALL_FAILED));
  if (!ok) LOG_ERR("FileMgr", "install %s failed: %s", fullPath.c_str(), err.c_str());
  requestUpdate();
}

// ---- item menu -----------------------------------------------------------

void FileManagerActivity::showItemMenu(const int entryIndex) {
  if (entryIndex < 0 || entryIndex >= static_cast<int>(entries_.size())) return;
  const Entry& entry = entries_[entryIndex];
  if (FsHelpers::isProtectedPathComponent(entry.name)) return;

  const char* openLabel = entry.isDir              ? tr(STR_FILEMGR_ENTER)
                          : isEappName(entry.name) ? tr(STR_FILEMGR_INSTALL)
                                                   : tr(STR_FILEMGR_OPEN);
  notice_.clear();
  const char* actions[] = {openLabel, tr(STR_FILEMGR_COPY), tr(STR_FILEMGR_CUT), tr(STR_RENAME), tr(STR_DELETE)};
  popup_.show(entry.name.c_str(), actions, static_cast<int>(std::size(actions)), 0,
              [this, entryIndex](const int choice) { runItemAction(entryIndex, static_cast<ItemAction>(choice)); });
  requestUpdate();
}

void FileManagerActivity::runItemAction(const int entryIndex, const ItemAction action) {
  if (entryIndex < 0 || entryIndex >= static_cast<int>(entries_.size())) return;
  const Entry& entry = entries_[entryIndex];
  const std::string full = pathFor(entry);

  switch (action) {
    case ItemAction::Open:
      if (entry.isDir)
        navigateTo(full);
      else
        openEntry(entry);
      return;
    case ItemAction::Copy:
      clipPath_ = full;
      clipIsCut_ = false;
      showNotice(tr(STR_FILEMGR_COPIED));
      return;
    case ItemAction::Cut:
      clipPath_ = full;
      clipIsCut_ = true;
      showNotice(tr(STR_FILEMGR_CUT_OK));
      return;
    case ItemAction::Rename:
      promptRename(entryIndex);
      return;
    case ItemAction::Delete:
      promptDelete(entryIndex);
      return;
  }
}

void FileManagerActivity::promptRename(const int entryIndex) {
  if (entryIndex < 0 || entryIndex >= static_cast<int>(entries_.size())) return;
  const std::string oldName = entries_[entryIndex].name;
  const std::string dir = path_;

  auto handler = [this, oldName, dir](const ActivityResult& result) {
    if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
    const std::string& name = std::get<KeyboardResult>(result.data).text;
    if (!FsHelpers::isValidPathComponent(name) || FsHelpers::isProtectedPathComponent(name)) {
      showNotice(tr(STR_INVALID_FILE_NAME));
      return;
    }
    const std::string from = FileOps::joinPath(dir, oldName);
    const std::string to = FileOps::joinPath(dir, name);
    if (from == to) return;
    if (Storage.exists(to.c_str())) {
      showNotice(tr(STR_TARGET_EXISTS));
      return;
    }
    if (!Storage.rename(from.c_str(), to.c_str())) {
      showNotice(tr(STR_FILE_OPERATION_FAILED));
      return;
    }
    navigateTo(dir, nav.selected);
  };
  startActivityForResultWith<KeyboardEntryActivity>(std::move(handler), tr(STR_RENAME), oldName, kMaxNameBytes,
                                                    InputType::Text);
}

void FileManagerActivity::promptDelete(const int entryIndex) {
  if (entryIndex < 0 || entryIndex >= static_cast<int>(entries_.size())) return;
  const Entry& entry = entries_[entryIndex];
  if (FsHelpers::isProtectedPathComponent(entry.name)) return;
  const std::string full = pathFor(entry);
  const std::string dir = path_;
  const int returnIndex = nav.selected;

  // Confirm on a separate activity, never by re-showing popup_ from inside its
  // own callback: OptionPopup reassigns onSelectCallback while that callback is
  // still on the stack, which would free the lambda mid-execution.
  auto handler = [this, full, dir, returnIndex](const ActivityResult& result) {
    if (result.isCancelled) return;
    showBlocking(tr(STR_FILEMGR_DELETING), FileOps::baseName(full).c_str());
    const bool ok = FileOps::removeTree(full);
    // The row under the cursor is gone; step back so the cursor stays in range.
    navigateTo(dir, returnIndex > 0 ? returnIndex - 1 : 0);
    if (!ok) showNotice(tr(STR_FILE_OPERATION_FAILED));
  };
  if (!startActivityForResultWith<ConfirmationActivity>(std::move(handler), std::string(tr(STR_FILEMGR_DELETE_CONFIRM)),
                                                        entry.name)) {
    showNotice(tr(STR_FILE_OPERATION_FAILED));
  }
}

// ---- directory menu ------------------------------------------------------

void FileManagerActivity::showDirMenu() {
  notice_.clear();
  const char* actions[] = {tr(STR_FILEMGR_NEW_FOLDER), tr(STR_FILEMGR_PASTE),
                           showHidden_ ? tr(STR_FILEMGR_HIDE_HIDDEN) : tr(STR_FILEMGR_SHOW_HIDDEN),
                           tr(STR_FILEMGR_STORAGE)};
  popup_.show(path_.c_str(), actions, static_cast<int>(std::size(actions)), 0,
              [this](const int choice) { runDirAction(static_cast<DirAction>(choice)); });
  requestUpdate();
}

void FileManagerActivity::runDirAction(const DirAction action) {
  switch (action) {
    case DirAction::NewFolder:
      promptNewFolder();
      return;
    case DirAction::Paste:
      doPaste();
      return;
    case DirAction::ToggleHidden:
      showHidden_ = !showHidden_;
      navigateTo(path_, 0);
      return;
    case DirAction::StorageInfo: {
      uint64_t total = 0, freeBytes = 0;
      char totalStr[24], freeStr[24], msg[80];
      if (Storage.getSpace(total, freeBytes)) {
        FileOps::formatSize(total, totalStr, sizeof(totalStr));
        FileOps::formatSize(freeBytes, freeStr, sizeof(freeStr));
        snprintf(msg, sizeof(msg), "%s / %s", freeStr, totalStr);
        showNotice(msg);
      } else {
        showNotice(tr(STR_FILE_OPERATION_FAILED));
      }
      return;
    }
  }
}

void FileManagerActivity::promptNewFolder() {
  const std::string dir = path_;
  auto handler = [this, dir](const ActivityResult& result) {
    if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
    const std::string& name = std::get<KeyboardResult>(result.data).text;
    if (!FsHelpers::isValidPathComponent(name) || FsHelpers::isProtectedPathComponent(name)) {
      showNotice(tr(STR_INVALID_FILE_NAME));
      return;
    }
    const std::string full = FileOps::joinPath(dir, name);
    if (Storage.exists(full.c_str())) {
      showNotice(tr(STR_TARGET_EXISTS));
      return;
    }
    if (!Storage.mkdir(full.c_str())) {
      showNotice(tr(STR_FILE_OPERATION_FAILED));
      return;
    }
    navigateTo(dir, 0);
  };
  startActivityForResultWith<KeyboardEntryActivity>(std::move(handler), tr(STR_FILEMGR_NEW_FOLDER), "", kMaxNameBytes,
                                                    InputType::Text);
}

void FileManagerActivity::doPaste() {
  if (clipPath_.empty()) {
    showNotice(tr(STR_FILEMGR_CLIPBOARD_EMPTY));
    return;
  }
  if (!Storage.exists(clipPath_.c_str())) {  // moved or deleted since it was cut
    clipPath_.clear();
    showNotice(tr(STR_FILEMGR_CLIPBOARD_EMPTY));
    return;
  }
  // Pasting a directory inside itself would recurse forever.
  if (FsHelpers::isSameOrDescendantPath(path_, clipPath_)) {
    showNotice(tr(STR_FILEMGR_PASTE_INTO_SELF));
    return;
  }
  // Cutting and pasting back into the same directory is a no-op, not a rename
  // to "name (2)".
  if (clipIsCut_ && FileOps::parentPath(clipPath_) == path_) {
    clipPath_.clear();
    showNotice(tr(STR_FILEMGR_PASTE_OK));
    return;
  }

  const std::string leaf = FileOps::baseName(clipPath_);
  const std::string target = FileOps::joinPath(path_, FileOps::uniqueNameIn(path_, leaf));

  showBlocking(tr(STR_FILEMGR_PASTING), leaf.c_str());
  bool ok = false;
  if (clipIsCut_ && FileOps::parentPath(clipPath_) != path_) {
    // A move within one volume is a rename; fall back to copy+delete when the
    // filesystem refuses it.
    ok = Storage.rename(clipPath_.c_str(), target.c_str());
  }
  if (!ok) {
    FileOps::CopyBuffer buf;
    ok = buf.valid() && FileOps::copyAny(clipPath_, target, buf);
    if (ok && clipIsCut_) ok = FileOps::removeTree(clipPath_);
  }
  if (ok && clipIsCut_) clipPath_.clear();  // a cut is consumed; a copy stays

  navigateTo(path_, nav.selected);
  showNotice(ok ? tr(STR_FILEMGR_PASTE_OK) : tr(STR_FILEMGR_PASTE_FAILED));
}
