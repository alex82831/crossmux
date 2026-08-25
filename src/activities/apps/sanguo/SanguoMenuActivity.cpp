#include "SanguoMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "SanguoEngine.h"
#include "SanguoGameActivity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kHelpText =
    "目标：夺取全部十二座城池，一统天下。\n\n"
    "每回合有三次行动：开垦（增粮）、通商（增金）、筑城（增防）、"
    "征兵（金300粮500得2000兵）、出征（向相邻城池进军）。\n\n"
    "战力由兵力、城防与坐镇武将的武力智力决定，守方有地利加成。"
    "攻下由武将镇守的空白城池可招揽其归顺。\n\n"
    "每回合结束按城池产出金粮，并按兵力消耗粮草；缺粮会导致士卒逃亡。"
    "魏起家三城地广，蜀吴两城而府库殷实，尽早抢占中立城池方为上策。";
}

void SanguoMenuActivity::onEnter() {
  Activity::onEnter();
  hasSave_ = sanguo::Engine::saveExists();
  selected_ = 0;
  view_ = View::Main;
  requestUpdate();
}

int SanguoMenuActivity::mainItemCount() const { return hasSave_ ? 4 : 3; }

void SanguoMenuActivity::loop() {
  const int count = view_ == View::Main ? mainItemCount() : view_ == View::FactionPick ? 3 : 0;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view_ == View::Main) {
      activityManager.goToApps();
    } else {
      view_ = View::Main;
      selected_ = 0;
      requestUpdate();
    }
    return;
  }
  if (view_ == View::Help) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      view_ = View::Main;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selected_ = static_cast<uint8_t>((selected_ + count - 1) % count);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selected_ = static_cast<uint8_t>((selected_ + 1) % count);
    requestUpdate();
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  if (view_ == View::FactionPick) {
    activityManager.startSanguoGame(selected_, true);
    return;
  }
  int item = selected_;
  if (!hasSave_) ++item;  // skip the hidden "continue" slot
  switch (item) {
    case 0:  // continue
      activityManager.startSanguoGame(0, false);
      return;
    case 1:  // new game
      view_ = View::FactionPick;
      selected_ = 0;
      break;
    case 2:  // help
      view_ = View::Help;
      break;
    case 3:  // back
    default:
      activityManager.goToApps();
      return;
  }
  requestUpdate();
}

void SanguoMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect fullScreen{0, 0, sw, renderer.getScreenHeight()};

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_SANGUO_TITLE));

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;

  if (view_ == View::Help) {
    UITheme::drawCenteredWrappedText(
        renderer, Rect{contentX, contentTop + metrics.verticalSpacing, contentW, safe.y + safe.height - contentTop},
        UI_10_FONT_ID, kHelpText, 24, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  } else {
    const char* mainItems[4];
    int count = 0;
    if (view_ == View::Main) {
      if (hasSave_) mainItems[count++] = tr(STR_SANGUO_CONTINUE);
      mainItems[count++] = tr(STR_SANGUO_NEW);
      mainItems[count++] = tr(STR_SANGUO_HELP);
      mainItems[count++] = tr(STR_BACK);
    } else {
      char labels[3][48];
      for (int f = 0; f < 3; ++f) {
        snprintf(labels[f], sizeof(labels[0]), "%s（%s）", sanguo::kFactionNames[f], sanguo::kFactionLeaders[f]);
      }
      // labels live on the stack; draw immediately below.
      const int rowH = metrics.menuRowHeight;
      int y = contentTop + metrics.verticalSpacing * 3;
      UITheme::drawCenteredText(renderer, fullScreen, UI_12_FONT_ID, y, tr(STR_SANGUO_PICK_FACTION), true,
                                EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing * 2;
      for (int i = 0; i < 3; ++i) {
        const bool sel = i == selected_;
        if (sel) renderer.fillRect(contentX, y, contentW, rowH, true);
        const int textY = y + (rowH - renderer.getTextHeight(UI_12_FONT_ID)) / 2;
        renderer.drawText(UI_12_FONT_ID, contentX + 16, textY, labels[i], !sel);
        y += rowH + metrics.verticalSpacing;
      }
      const auto labelsBtn = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
      GUI.drawButtonHints(renderer, labelsBtn.btn1, labelsBtn.btn2, labelsBtn.btn3, labelsBtn.btn4);
      renderer.displayBuffer();
      return;
    }
    const int rowH = metrics.menuRowHeight;
    int y = contentTop + metrics.verticalSpacing * 4;
    for (int i = 0; i < count; ++i) {
      const bool sel = i == static_cast<int>(selected_);
      if (sel) renderer.fillRect(contentX, y, contentW, rowH, true);
      const int textY = y + (rowH - renderer.getTextHeight(UI_12_FONT_ID)) / 2;
      renderer.drawText(UI_12_FONT_ID, contentX + 16, textY, mainItems[i], !sel);
      y += rowH + metrics.verticalSpacing;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
