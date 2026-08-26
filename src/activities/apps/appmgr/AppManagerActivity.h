#pragma once

#include <string>

#include "DynAppRegistry.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// 应用管理 (App Manager): install, update, run and uninstall dynamic apps
// (.eapp on SD), reach the online catalog, and jump to the built-in app
// visibility settings. Two screens share this activity: the installed list
// and the online catalog (rows rebuilt on switch, same nav shell).
class AppManagerActivity final : public UiListActivity {
 public:
  AppManagerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("AppMgr", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  enum class Mode : uint8_t { Installed, Catalog };
  // Row action ids (ListItem.actionValue): stable across rebuilds.
  static constexpr int kActionCatalog = 1000;
  static constexpr int kActionBrowserHint = 1001;
  static constexpr int kActionBuiltinVisibility = 1002;
  static constexpr int kActionInstallFromDisk = 1003;
  static constexpr int kActionCatalogServer = 1004;

  int listCount() const override { return rowCount_; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void drawFooter() override;

  int installedIndexForRow(int index) const;
  int selectedInstalledIndex();
  void rebuildInstalledRows();
  void openCatalog();
  void rebuildCatalogRows();
  void showAppActions(int installedIndex);
  void runApp(const char* slug);
  void installEntry(int catalogIndex);
  void showBlockingStatus(const char* line1, const char* line2);
  void pickFromDisk();
  void editCatalogServer();

  Mode mode_ = Mode::Installed;

  dynappreg::InstalledApp installed_[dynappreg::kMaxApps];
  int installedCount_ = 0;
  dynappreg::CatalogEntry catalog_[dynappreg::kMaxCatalog];
  int catalogCount_ = 0;
  std::string catalogError_;

  // Row window shared by both modes; strings owned here because ListItem
  // keeps only pointers. The two modes rebuild it from scratch and never
  // coexist, so this is the larger of the two, not their sum.
  static constexpr int kMaxRows =
      (dynappreg::kMaxApps > dynappreg::kMaxCatalog ? dynappreg::kMaxApps : dynappreg::kMaxCatalog) + 8;
  freeink::ui::ListItem rowItems_[kMaxRows]{};
  std::string rowTitles_[kMaxRows];
  std::string rowSubtitles_[kMaxRows];
  int rowCount_ = 0;

  OptionPopup optionPopup_;
  bool hintVisible_ = false;  // browser-install instructions overlay
};
