#pragma once

#include <I18n.h>

#include <string>

#include "../../components/themes/BaseTheme.h"  // UIIcon
#include "../../util/ButtonNavigator.h"
#include "../Activity.h"
#ifndef SIMULATOR
#include "appmgr/DynAppRegistry.h"
#endif

// Apps menu — the single entry-point on the home screen for every sub-app.
//
// Two sources feed one list. The built-ins are the constexpr `kAppEntries`
// table in AppsMenuActivity.cpp (add one by assigning a stable AppId,
// appending a row, and adding goTo<App>() in ActivityManager). Installed
// .eapp files are scanned off the card on entry and appended after them, so an
// app the user installs shows up here and not only inside 应用管理.
// See src/activities/apps/README.md.
class AppsMenuActivity final : public Activity {
 public:
  AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}
  ~AppsMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return MainTab::Apps; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;

  static int getAppCount();
  static StrId getAppTitleId(int appIndex);
  static bool isAppVisible(int appIndex);
  static bool setAppVisible(int appIndex, bool visible);

 private:
  // Built-in entries only; installed apps are appended after these.
  static int visibleBuiltinCount();
  static int builtinIndexForVisible(int visibleIndex);

  // The whole list, built-ins first. Indices below installedFirstIndex() are
  // built-ins; the rest index installed_.
  int entryCount() const;
  int installedFirstIndex() const;
  std::string entryTitle(int index) const;
  UIIcon entryIcon(int index) const;

  void scanInstalled();
  bool usesIconLayout() const;
  int iconIndexFromPoint(int x, int y) const;
  void openSelected();
  void drawIconGrid(const Rect& rect, int visibleCount, bool showSelection) const;

  ButtonNavigator buttonNavigator;
  int selected = 0;

#ifndef SIMULATOR
  // ~2KB, live only while the Apps tab is open (the activity is replaced, not
  // kept). Fixed arrays rather than strings: 24 slugs and names would
  // otherwise be up to 48 small heap allocations on a 380KB budget.
  dynappreg::InstalledAppName installed_[dynappreg::kMaxApps];
#endif
  int installedCount_ = 0;
};
