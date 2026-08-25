#pragma once

#include <cstdint>

#include "SanguoEngine.h"
#include "activities/Activity.h"

// 三国霸业 main screen: the scrolling city roster with per-city action menus
// (develop / recruit / march), battle and round reports, and win/defeat
// handling. All rules live in sanguo::Engine; this class renders and routes
// input. CN-build only (ENABLE_CHINESE_VERSION).
class SanguoGameActivity final : public Activity {
 public:
  SanguoGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint8_t faction, bool newGame)
      : Activity("SanguoGame", renderer, mappedInput), faction_(faction), newGame_(newGame) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t {
    CityList,
    ActionMenu,
    TargetMenu,
    ForceMenu,
    CityInfo,
    BattleReport,
    RoundReport,
    GameOver,
  };

  void handleListInput();
  void handleActionMenu();
  void handleTargetMenu();
  void handleForceMenu();
  void endTurn();
  void afterStateChange();
  void setNotice(const char* text);
  int adjacentTargets(int fromCity, uint8_t* out, int maxOut) const;
  int generalsAt(int city, uint8_t faction) const;
  void drawCityList();
  void drawPanelText(const char* title, const char* body);
  void drawCityInfo();

  sanguo::Engine engine_;
  uint8_t faction_;
  bool newGame_;

  View view_ = View::CityList;
  int cursor_ = 0;
  int windowStart_ = 0;
  uint8_t menuSel_ = 0;
  int fromCity_ = 0;
  int targetCity_ = 0;
  uint8_t targets_[4] = {};
  int targetCount_ = 0;
  char notice_[64] = {};

  // Panel text + menu label pools live on the heap-allocated activity, not
  // the loop task stack (see heap-discipline: frames stay lean).
  char panelBody_[520] = {};
  char menuLabels_[5][64] = {};

  sanguo::BattleReport battle_;
  sanguo::RoundLog roundLog_;
  sanguo::BattleReport aiBattles_[4];
  int aiBattleCount_ = 0;
  int outcome_ = 0;
};
