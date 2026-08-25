#include "VocabActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "VocabData.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kProgressPath = "/.crosspoint/vocab.bin";
constexpr uint32_t kMagic = 0x31434F56;  // "VOC1"

struct ProgressHeader {
  uint32_t magic = kMagic;
  uint16_t count = 0;
  uint16_t reserved = 0;
};
}  // namespace

void VocabActivity::onEnter() {
  Activity::onEnter();
  loadProgress();
  pickNext();
  requestUpdate();
}

void VocabActivity::onExit() {
  if (dirty_) saveProgress();
  Activity::onExit();
}

void VocabActivity::countBoxes(int& fresh, int& learning, int& mastered) const {
  fresh = learning = mastered = 0;
  for (int i = 0; i < kVocabWordCount; ++i) {
    if (box_[i] == 0) ++fresh;
    else if (box_[i] == 1) ++learning;
    else ++mastered;
  }
}

void VocabActivity::pickNext() {
  // Weighted Leitner draw: unknown 6 : learning 3 : mastered 1. Walk from a
  // random start so the pass stays O(n) without building candidate lists.
  uint32_t totalWeight = 0;
  for (int i = 0; i < kVocabWordCount; ++i) {
    totalWeight += box_[i] == 0 ? 6 : box_[i] == 1 ? 3 : 1;
  }
  for (int attempt = 0; attempt < 4; ++attempt) {
    uint32_t target = esp_random() % totalWeight;
    for (int i = 0; i < kVocabWordCount; ++i) {
      const uint32_t w = box_[i] == 0 ? 6 : box_[i] == 1 ? 3 : 1;
      if (target < w) {
        if (i == previous_ && attempt < 3) break;  // avoid immediate repeats
        current_ = i;
        previous_ = i;
        revealed_ = false;
        return;
      }
      target -= w;
    }
  }
  current_ = static_cast<int>(esp_random() % kVocabWordCount);
  previous_ = current_;
  revealed_ = false;
}

void VocabActivity::grade(const bool knew) {
  if (!revealed_) return;
  if (knew) {
    if (box_[current_] < 2) ++box_[current_];
  } else {
    box_[current_] = 0;
  }
  dirty_ = true;
  ++sessionSeen_;
  if ((sessionSeen_ % 10) == 0) saveProgress();  // checkpoint every 10 cards
  pickNext();
  requestUpdate();
}

void VocabActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!revealed_) {
      revealed_ = true;
    } else {
      pickNext();  // skip grading, next card
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    grade(true);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    grade(false);
  }
}

void VocabActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect fullScreen{0, 0, sw, sh};

  renderer.clearScreen();
  int fresh = 0, learning = 0, mastered = 0;
  countBoxes(fresh, learning, mastered);
  char subtitle[32];
  snprintf(subtitle, sizeof(subtitle), "%d/%d", mastered, kVocabWordCount);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_VOCAB_TITLE), subtitle);

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;
  const VocabWord& w = kVocabWords[current_];

  int y = contentTop + metrics.verticalSpacing * 3;
  UITheme::drawCenteredText(renderer, fullScreen, NOTOSANS_18_FONT_ID, y, w.word, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + metrics.verticalSpacing;
  UITheme::drawCenteredText(renderer, fullScreen, UI_10_FONT_ID, y, w.pos);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;

  // Box marker for the current card.
  const char* boxText = box_[current_] == 0   ? tr(STR_VOCAB_NEW)
                        : box_[current_] == 1 ? tr(STR_VOCAB_LEARNING)
                                              : tr(STR_VOCAB_MASTERED);
  UITheme::drawCenteredText(renderer, fullScreen, SMALL_FONT_ID, y, boxText);
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;

  if (revealed_) {
    UITheme::drawCenteredWrappedText(renderer, Rect{contentX, y, contentW, renderer.getLineHeight(UI_12_FONT_ID) * 3},
                                     UI_12_FONT_ID, w.meaning, 3, true, EpdFontFamily::BOLD);
  } else {
    UITheme::drawCenteredText(renderer, fullScreen, UI_10_FONT_ID, y, tr(STR_VOCAB_REVEAL_HINT));
  }

  char stats[96];
  snprintf(stats, sizeof(stats), "%s %d · %s %d · %s %d", tr(STR_VOCAB_NEW), fresh, tr(STR_VOCAB_LEARNING), learning,
           tr(STR_VOCAB_MASTERED), mastered);
  UITheme::drawCenteredText(renderer, fullScreen, UI_10_FONT_ID,
                            safe.y + safe.height - renderer.getLineHeight(UI_10_FONT_ID), stats);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), revealed_ ? tr(STR_VOCAB_NEXT) : tr(STR_VOCAB_REVEAL),
                                            tr(STR_VOCAB_KNOW), tr(STR_VOCAB_DONT_KNOW));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void VocabActivity::loadProgress() {
  memset(box_, 0, sizeof(box_));
  ProgressHeader header;
  HalFile file;
  if (!Storage.openFileForRead("VOC", kProgressPath, file)) return;
  if (file.read(&header, sizeof(header)) != static_cast<int>(sizeof(header)) || header.magic != kMagic) return;
  const int n = header.count < kVocabWordCount ? header.count : kVocabWordCount;
  file.read(box_, static_cast<size_t>(n));
  for (int i = 0; i < kVocabWordCount; ++i) {
    if (box_[i] > 2) box_[i] = 0;
  }
}

void VocabActivity::saveProgress() const {
  ProgressHeader header;
  header.count = static_cast<uint16_t>(kVocabWordCount);
  HalFile file;
  if (!Storage.openFileForWrite("VOC", kProgressPath, file)) {
    LOG_ERR("VOC", "progress open failed");
    return;
  }
  file.write(&header, sizeof(header));
  file.write(box_, static_cast<size_t>(kVocabWordCount));
}
