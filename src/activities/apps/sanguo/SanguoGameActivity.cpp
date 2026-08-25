#include "SanguoGameActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/apps/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

using sanguo::kCities;
using sanguo::kCityCount;
using sanguo::kFactionNames;
using sanguo::kGeneralCount;
using sanguo::kGenerals;

void SanguoGameActivity::onEnter() {
  Activity::onEnter();
  if (newGame_) {
    engine_.newGame(faction_);
    engine_.save();
  } else if (!engine_.load()) {
    engine_.newGame(faction_);
  }
  cursor_ = 0;
  windowStart_ = 0;
  view_ = View::CityList;
  notice_[0] = '\0';
  requestUpdate();
}

void SanguoGameActivity::onExit() {
  if (engine_.outcome() == 0) engine_.save();
  Activity::onExit();
}

void SanguoGameActivity::setNotice(const char* text) {
  snprintf(notice_, sizeof(notice_), "%s", text);
}

int SanguoGameActivity::adjacentTargets(const int fromCity, uint8_t* out, const int maxOut) const {
  int n = 0;
  for (int t = 0; t < kCityCount && n < maxOut; ++t) {
    if ((kCities[fromCity].adjacency & (1U << t)) != 0) out[n++] = static_cast<uint8_t>(t);
  }
  return n;
}

int SanguoGameActivity::generalsAt(const int city, const uint8_t faction) const {
  int n = 0;
  for (int g = 0; g < kGeneralCount; ++g) {
    if (engine_.st.generalCity[g] == city && engine_.st.generalOwner[g] == faction) ++n;
  }
  return n;
}

void SanguoGameActivity::afterStateChange() {
  outcome_ = engine_.outcome();
  if (outcome_ != 0) {
    view_ = View::GameOver;
    sanguo::Engine::wipeSave();
  }
  requestUpdate();
}

void SanguoGameActivity::loop() {
  switch (view_) {
    case View::CityList:
      handleListInput();
      return;
    case View::ActionMenu:
      handleActionMenu();
      return;
    case View::TargetMenu:
      handleTargetMenu();
      return;
    case View::ForceMenu:
      handleForceMenu();
      return;
    case View::CityInfo:
    case View::BattleReport:
    case View::RoundReport:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
          mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        view_ = View::CityList;
        afterStateChange();
      }
      return;
    case View::GameOver:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
          mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        activityManager.goToSanguo();
      }
      return;
  }
}

void SanguoGameActivity::handleListInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToSanguo();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    cursor_ = (cursor_ + kCityCount - 1) % kCityCount;
    notice_[0] = '\0';
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    cursor_ = (cursor_ + 1) % kCityCount;
    notice_[0] = '\0';
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    endTurn();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (engine_.cityIsPlayers(cursor_)) {
      view_ = View::ActionMenu;
      menuSel_ = 0;
    } else {
      view_ = View::CityInfo;
    }
    requestUpdate();
  }
}

void SanguoGameActivity::handleActionMenu() {
  static constexpr int kCount = 7;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect panel = gameMenuPanelRect(renderer.getScreenWidth(), renderer.getScreenHeight(), 300,
                                       metrics.menuRowHeight, metrics.menuRowHeight, kCount);
  const auto result =
      gameHandleMenuInput(mappedInput, panel, metrics.menuRowHeight, metrics.menuRowHeight, kCount, menuSel_);
  if (result == GameMenuInputResult::SelectionChanged) {
    requestUpdate();
    return;
  }
  if (result == GameMenuInputResult::Dismissed) {
    view_ = View::CityList;
    requestUpdate();
    return;
  }
  if (result != GameMenuInputResult::Activated) return;

  sanguo::ActionResult r = sanguo::ActionResult::Ok;
  switch (menuSel_) {
    case 0:
    case 1:
    case 2:
      r = engine_.develop(cursor_, menuSel_);
      view_ = View::CityList;
      break;
    case 3:
      r = engine_.recruit(cursor_);
      view_ = View::CityList;
      break;
    case 4:  // march
      if (engine_.st.actionsLeft == 0) {
        r = sanguo::ActionResult::NoActions;
        view_ = View::CityList;
        break;
      }
      fromCity_ = cursor_;
      targetCount_ = adjacentTargets(fromCity_, targets_, 4);
      menuSel_ = 0;
      view_ = View::TargetMenu;
      break;
    case 5:  // inspect
      view_ = View::CityInfo;
      break;
    case 6:  // cancel
    default:
      view_ = View::CityList;
      break;
  }
  switch (r) {
    case sanguo::ActionResult::NoActions:
      setNotice(tr(STR_SANGUO_NO_ACTIONS));
      break;
    case sanguo::ActionResult::NoGold:
      setNotice(tr(STR_SANGUO_NO_GOLD));
      break;
    case sanguo::ActionResult::NoFood:
      setNotice(tr(STR_SANGUO_NO_FOOD));
      break;
    case sanguo::ActionResult::MaxedOut:
      setNotice(tr(STR_SANGUO_MAXED));
      break;
    case sanguo::ActionResult::Ok:
    case sanguo::ActionResult::Invalid:
    default:
      break;
  }
  if (view_ == View::CityList) engine_.save();
  requestUpdate();
}

void SanguoGameActivity::handleTargetMenu() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int count = targetCount_ + 1;  // + cancel
  const Rect panel = gameMenuPanelRect(renderer.getScreenWidth(), renderer.getScreenHeight(), 320,
                                       metrics.menuRowHeight, metrics.menuRowHeight, count);
  const auto result =
      gameHandleMenuInput(mappedInput, panel, metrics.menuRowHeight, metrics.menuRowHeight, count, menuSel_);
  if (result == GameMenuInputResult::SelectionChanged) {
    requestUpdate();
    return;
  }
  if (result == GameMenuInputResult::Dismissed) {
    view_ = View::CityList;
    requestUpdate();
    return;
  }
  if (result != GameMenuInputResult::Activated) return;
  if (menuSel_ >= targetCount_) {
    view_ = View::CityList;
  } else {
    targetCity_ = targets_[menuSel_];
    menuSel_ = 0;
    view_ = View::ForceMenu;
  }
  requestUpdate();
}

void SanguoGameActivity::handleForceMenu() {
  static constexpr int kCount = 5;  // 25/50/75/90% + cancel
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect panel = gameMenuPanelRect(renderer.getScreenWidth(), renderer.getScreenHeight(), 300,
                                       metrics.menuRowHeight, metrics.menuRowHeight, kCount);
  const auto result =
      gameHandleMenuInput(mappedInput, panel, metrics.menuRowHeight, metrics.menuRowHeight, kCount, menuSel_);
  if (result == GameMenuInputResult::SelectionChanged) {
    requestUpdate();
    return;
  }
  if (result == GameMenuInputResult::Dismissed) {
    view_ = View::CityList;
    requestUpdate();
    return;
  }
  if (result != GameMenuInputResult::Activated) return;
  if (menuSel_ >= 4) {
    view_ = View::CityList;
    requestUpdate();
    return;
  }
  static constexpr int kPercent[4] = {25, 50, 75, 90};
  const uint16_t troops =
      static_cast<uint16_t>(static_cast<uint32_t>(engine_.st.cities[fromCity_].troops) * kPercent[menuSel_] / 100);
  battle_ = sanguo::BattleReport{};
  const auto r = engine_.march(fromCity_, targetCity_, troops, battle_);
  if (r != sanguo::ActionResult::Ok) {
    setNotice(r == sanguo::ActionResult::NoActions ? tr(STR_SANGUO_NO_ACTIONS) : tr(STR_SANGUO_TOO_FEW));
    view_ = View::CityList;
  } else {
    engine_.save();
    view_ = battle_.happened ? View::BattleReport : View::CityList;
    if (!battle_.happened) setNotice(tr(STR_SANGUO_MOVED));
  }
  if (view_ == View::CityList) afterStateChange();
  requestUpdate();
}

void SanguoGameActivity::endTurn() {
  roundLog_ = sanguo::RoundLog{};
  aiBattleCount_ = 0;
  engine_.endPlayerTurn(roundLog_, aiBattles_, aiBattleCount_, 4);
  view_ = View::RoundReport;
  notice_[0] = '\0';
  requestUpdate();
}

void SanguoGameActivity::drawPanelText(const char* title, const char* body) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int panelW = sw * 86 / 100;
  const int panelH = sh * 55 / 100;
  const Rect panel{(sw - panelW) / 2, (sh - panelH) / 2, panelW, panelH};
  renderer.fillRect(panel.x, panel.y, panel.width, panel.height, false);
  renderer.drawRect(panel.x, panel.y, panel.width, panel.height, true);
  renderer.drawRect(panel.x + 2, panel.y + 2, panel.width - 4, panel.height - 4, true);
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  UITheme::drawCenteredText(renderer, Rect{panel.x, 0, panel.width, sh}, UI_12_FONT_ID,
                            panel.y + metrics.verticalSpacing, title, true, EpdFontFamily::BOLD);
  UITheme::drawCenteredWrappedText(
      renderer,
      Rect{panel.x + 14, panel.y + metrics.verticalSpacing + titleH + 6, panel.width - 28,
           panel.height - titleH - metrics.verticalSpacing * 2 - 12},
      UI_10_FONT_ID, body, 12, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
}

void SanguoGameActivity::drawCityInfo() {
  const sanguo::CityState& c = engine_.st.cities[cursor_];
  char* body = panelBody_;
  int pos = snprintf(body, sizeof(panelBody_), "%s：%s\n%s %u\n%s %u  %s %u  %s %u\n", tr(STR_SANGUO_OWNER),
                     kFactionNames[c.owner], tr(STR_SANGUO_TROOPS), c.troops, tr(STR_SANGUO_FARM), c.farm,
                     tr(STR_SANGUO_MARKET), c.market, tr(STR_SANGUO_WALLS), c.walls);
  bool anyGeneral = false;
  for (int g = 0; g < kGeneralCount && pos < static_cast<int>(sizeof(panelBody_)) - 40; ++g) {
    if (engine_.st.generalCity[g] != cursor_ || engine_.st.generalOwner[g] != c.owner) continue;
    if (!anyGeneral) {
      pos += snprintf(body + pos, sizeof(panelBody_) - pos, "\n%s\n", tr(STR_SANGUO_GENERALS));
      anyGeneral = true;
    }
    pos += snprintf(body + pos, sizeof(panelBody_) - pos, "%s 武%u 智%u\n", kGenerals[g].name, kGenerals[g].war,
                    kGenerals[g].wit);
  }
  drawPanelText(kCities[cursor_].name, body);
}

void SanguoGameActivity::drawCityList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  const sanguo::FactionState& f = engine_.st.factions[engine_.st.playerFaction];
  char title[48];
  char subtitle[64];
  snprintf(title, sizeof(title), "%s%u%s · %s", tr(STR_SANGUO_ROUND_PRE), engine_.st.round, tr(STR_SANGUO_ROUND_POST),
           kFactionNames[engine_.st.playerFaction]);
  snprintf(subtitle, sizeof(subtitle), "%s%u %s%u %s%u/%d", tr(STR_SANGUO_GOLD), f.gold, tr(STR_SANGUO_FOOD), f.food,
           tr(STR_SANGUO_ACTIONS), engine_.st.actionsLeft, sanguo::kActionsPerTurn);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, title, subtitle);

  int y = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing / 2;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;
  const int noticeH = notice_[0] != '\0' ? renderer.getLineHeight(UI_10_FONT_ID) : 0;
  if (noticeH > 0) {
    UITheme::drawCenteredText(renderer, Rect{0, 0, sw, renderer.getScreenHeight()}, UI_10_FONT_ID, y, notice_, true,
                              EpdFontFamily::BOLD);
    y += noticeH + 2;
  }

  const int rowH = renderer.getTextHeight(UI_10_FONT_ID) + 10;
  const int availH = safe.y + safe.height - y;
  int visible = availH / rowH;
  if (visible < 3) visible = 3;
  if (visible > kCityCount) visible = kCityCount;
  if (cursor_ < windowStart_) windowStart_ = cursor_;
  if (cursor_ >= windowStart_ + visible) windowStart_ = cursor_ - visible + 1;

  char buf[96];
  for (int row = 0; row < visible; ++row) {
    const int i = windowStart_ + row;
    if (i >= kCityCount) break;
    const sanguo::CityState& c = engine_.st.cities[i];
    const bool sel = i == cursor_;
    if (sel) renderer.fillRect(contentX - 4, y - 2, contentW + 8, rowH, true);
    const bool mine = c.owner == engine_.st.playerFaction;
    snprintf(buf, sizeof(buf), "%s\xEF\xBC\xBB%s\xEF\xBC\xBD", kCities[i].name, kFactionNames[c.owner]);
    renderer.drawText(UI_10_FONT_ID, contentX, y, buf, !sel, mine ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    snprintf(buf, sizeof(buf), "%s%u", tr(STR_SANGUO_TROOPS), c.troops);
    renderer.drawText(UI_10_FONT_ID, contentX + contentW * 38 / 100, y, buf, !sel);
    const int gens = generalsAt(i, c.owner);
    snprintf(buf, sizeof(buf), "%u/%u/%u %s%d", c.farm, c.market, c.walls, tr(STR_SANGUO_GENERAL_SHORT), gens);
    renderer.drawText(UI_10_FONT_ID, contentX + contentW - renderer.getTextWidth(UI_10_FONT_ID, buf), y, buf, !sel);
    y += rowH;
  }
}

void SanguoGameActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawCityList();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  if (view_ == View::ActionMenu) {
    const GameMenuItem items[7] = {{tr(STR_SANGUO_DEV_FARM), nullptr},   {tr(STR_SANGUO_DEV_MARKET), nullptr},
                                   {tr(STR_SANGUO_DEV_WALLS), nullptr},  {tr(STR_SANGUO_RECRUIT), nullptr},
                                   {tr(STR_SANGUO_MARCH), nullptr},      {tr(STR_SANGUO_INSPECT), nullptr},
                                   {tr(STR_CANCEL), nullptr}};
    const Rect panel = gameMenuPanelRect(sw, sh, 300, metrics.menuRowHeight, metrics.menuRowHeight, 7);
    gameDrawMenu(renderer, panel, metrics.menuRowHeight, metrics.menuRowHeight, kCities[cursor_].name, items, 7,
                 menuSel_);
  } else if (view_ == View::TargetMenu) {
    GameMenuItem items[5];
    char (&labels)[5][64] = menuLabels_;
    for (int i = 0; i < targetCount_; ++i) {
      const int t = targets_[i];
      snprintf(labels[i], sizeof(labels[0]), "%s\xEF\xBC\xBB%s\xEF\xBC\xBD %u", kCities[t].name,
               kFactionNames[engine_.st.cities[t].owner], engine_.st.cities[t].troops);
      items[i] = {labels[i], nullptr};
    }
    items[targetCount_] = {tr(STR_CANCEL), nullptr};
    const Rect panel = gameMenuPanelRect(sw, sh, 320, metrics.menuRowHeight, metrics.menuRowHeight, targetCount_ + 1);
    gameDrawMenu(renderer, panel, metrics.menuRowHeight, metrics.menuRowHeight, tr(STR_SANGUO_MARCH_TO), items,
                 targetCount_ + 1, menuSel_);
  } else if (view_ == View::ForceMenu) {
    char (&labels)[5][64] = menuLabels_;
    GameMenuItem items[5];
    static constexpr int kPercent[4] = {25, 50, 75, 90};
    for (int i = 0; i < 4; ++i) {
      const uint32_t troops = static_cast<uint32_t>(engine_.st.cities[fromCity_].troops) * kPercent[i] / 100;
      snprintf(labels[i], sizeof(labels[0]), "%d%%（%u）", kPercent[i], static_cast<unsigned>(troops));
      items[i] = {labels[i], nullptr};
    }
    items[4] = {tr(STR_CANCEL), nullptr};
    const Rect panel = gameMenuPanelRect(sw, sh, 300, metrics.menuRowHeight, metrics.menuRowHeight, 5);
    gameDrawMenu(renderer, panel, metrics.menuRowHeight, metrics.menuRowHeight, tr(STR_SANGUO_FORCE), items, 5,
                 menuSel_);
  } else if (view_ == View::CityInfo) {
    drawCityInfo();
  } else if (view_ == View::BattleReport) {
    char* body = panelBody_;
    int pos = snprintf(body, sizeof(panelBody_), "%s %s → %s\n%s %u（%s %u）\n%s %u（%s %u）\n\n%s",
                       kFactionNames[battle_.attackerFaction], kCities[battle_.fromCity].name,
                       kCities[battle_.targetCity].name, tr(STR_SANGUO_ATK_SENT), battle_.attackerSent,
                       tr(STR_SANGUO_LOST), battle_.attackerLost, tr(STR_SANGUO_DEF_HAD), battle_.defenderHad,
                       tr(STR_SANGUO_LOST), battle_.defenderLost,
                       battle_.captured ? tr(STR_SANGUO_CITY_TAKEN) : tr(STR_SANGUO_REPELLED));
    if (battle_.joinedGeneral >= 0 && pos < static_cast<int>(sizeof(panelBody_)) - 48) {
      snprintf(body + pos, sizeof(panelBody_) - pos, "\n%s%s", kGenerals[battle_.joinedGeneral].name,
               tr(STR_SANGUO_JOINED));
    }
    drawPanelText(tr(STR_SANGUO_BATTLE), body);
  } else if (view_ == View::RoundReport) {
    char* body = panelBody_;
    int pos = snprintf(body, sizeof(panelBody_), "%s%u%s\n", tr(STR_SANGUO_ROUND_PRE), engine_.st.round,
                       tr(STR_SANGUO_ROUND_DONE));
    for (int i = 0; i < roundLog_.count && pos < static_cast<int>(sizeof(panelBody_)) - 80; ++i) {
      pos += snprintf(body + pos, sizeof(panelBody_) - pos, "%s\n", roundLog_.lines[i]);
    }
    if (roundLog_.count == 0) {
      pos += snprintf(body + pos, sizeof(panelBody_) - pos, "%s\n", tr(STR_SANGUO_QUIET_ROUND));
    }
    drawPanelText(tr(STR_SANGUO_ROUND_REPORT), body);
  } else if (view_ == View::GameOver) {
    drawPanelText(outcome_ == 1 ? tr(STR_SANGUO_VICTORY) : tr(STR_SANGUO_DEFEAT),
                  outcome_ == 1 ? tr(STR_SANGUO_VICTORY_BODY) : tr(STR_SANGUO_DEFEAT_BODY));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_SANGUO_END_TURN),
                                            tr(STR_SANGUO_END_TURN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
