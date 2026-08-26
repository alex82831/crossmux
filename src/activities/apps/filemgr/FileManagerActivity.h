#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// 文件管理 (File Manager): a general-purpose browser over the whole SD card.
//
// The Library's FileBrowserActivity is a *book picker* — it only lists the
// formats the reader opens, which is why /apps looks empty there. This one
// lists everything, and adds the operations a file manager is expected to
// have: copy/cut/paste, rename, delete, new folder, and open-by-type
// (选中 .eapp 即安装该应用).
//
// Input (one gesture, one action):
//   Up/Down   move           Confirm  enter dir / open file / go up on ".."
//   Back      up one level (exits at the root)
//   Left      directory menu (new folder, paste, hidden files, storage info)
//   long-press row  item menu (open/install, copy, cut, rename, delete)
//
// PickEapp mode reuses the same browser as a picker for the App Manager's
// install-from-disk flow: rows are the same, but activating a .eapp returns
// its path as the activity result instead of installing it here.
class FileManagerActivity final : public UiListActivity {
 public:
  enum class Mode : uint8_t { Manage, PickEapp };

  FileManagerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::Manage,
                      std::string initialPath = "/")
      : UiListActivity("FileMgr", renderer, mappedInput),
        mode_(mode),
        path_(initialPath.empty() ? "/" : std::move(initialPath)) {}

  void onEnter() override;
  void onExit() override;

 private:
  struct Entry {
    std::string name;
    bool isDir = false;
    uint64_t size = 0;
  };

  // Item-menu actions, in popup order.
  enum class ItemAction : uint8_t { Open, Copy, Cut, Rename, Delete };
  // Directory-menu actions, in popup order.
  enum class DirAction : uint8_t { NewFolder, Paste, ToggleHidden, StorageInfo };

  // The option popup is an overlay that owns the whole frame while it is up.
  // Without this override it goes active and swallows input while never being
  // drawn — the button appears dead and every press after it is eaten.
  void render(RenderLock&&) override;

  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void drawFooter() override;

  // Directory state. loadEntries() rebuilds both the entry list and the row
  // caches; callers must hold a RenderLock because buildScreen() reads them
  // on the render task.
  void loadEntries();
  void rebuildRows();
  void navigateTo(const std::string& path, int selectIndex = 0);
  void goUp();

  // Row 0 is ".." whenever we are below the root.
  bool hasParentRow() const { return path_ != "/"; }
  int entryIndexFor(int row) const { return hasParentRow() ? row - 1 : row; }
  std::string pathFor(const Entry& entry) const;

  void showItemMenu(int entryIndex);
  void runItemAction(int entryIndex, ItemAction action);
  void showDirMenu();
  void runDirAction(DirAction action);

  void openEntry(const Entry& entry);
  void installEapp(const std::string& fullPath);
  void promptNewFolder();
  void promptRename(int entryIndex);
  void promptDelete(int entryIndex);
  void doPaste();
  void showNotice(const char* message);
  void showBlocking(const char* message, const char* detail);

  Mode mode_ = Mode::Manage;
  std::string path_ = "/";
  std::vector<Entry> entries_;

  // Row caches: rebuilt on directory load, not per repaint — ListItem keeps
  // only pointers, and a 500-file directory must not re-derive 500 strings on
  // every cursor move.
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowSubs_;
  std::vector<freeink::ui::ListItem> rowItems_;

  // Clipboard, kept across directory changes for the life of the activity.
  std::string clipPath_;
  bool clipIsCut_ = false;

  bool showHidden_ = false;
  OptionPopup popup_;
  std::string notice_;  // transient one-line message drawn over the footer
};
