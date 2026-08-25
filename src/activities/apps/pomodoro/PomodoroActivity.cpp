#include "PomodoroActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kStatsPath = "/.crosspoint/pomodoro.bin";
constexpr uint32_t kStatsMagic = 0x314D4F50;  // "POM1"

struct StatsRecord {
  uint32_t magic = kStatsMagic;
  uint32_t ymd = 0;
  uint16_t today = 0;
  uint16_t reserved = 0;
  uint32_t total = 0;
};

constexpr uint32_t kMinValidEpoch = 1600000000;
}  // namespace

void PomodoroActivity::onEnter() {
  Activity::onEnter();
  loadStats();
  startPhase(Phase::Work, false);
  requestUpdate();
}

void PomodoroActivity::onExit() {
  saveStats();
  Activity::onExit();
}

uint32_t PomodoroActivity::phaseDurationMs(const Phase phase) const {
  const Preset& p = kPresets[presetIndex_];
  switch (phase) {
    case Phase::Work:
      return static_cast<uint32_t>(p.workMin) * 60000U;
    case Phase::ShortBreak:
      return static_cast<uint32_t>(p.shortMin) * 60000U;
    case Phase::LongBreak:
    default:
      return static_cast<uint32_t>(p.longMin) * 60000U;
  }
}

uint32_t PomodoroActivity::remainingMs() const {
  if (!running_) return pauseRemainMs_;
  const uint32_t now = millis();
  return (phaseEndMillis_ > now) ? phaseEndMillis_ - now : 0;
}

void PomodoroActivity::startPhase(const Phase phase, const bool autoRun) {
  phase_ = phase;
  pauseRemainMs_ = phaseDurationMs(phase);
  running_ = autoRun;
  if (autoRun) phaseEndMillis_ = millis() + pauseRemainMs_;
  lastShownRemainMs_ = pauseRemainMs_;
}

void PomodoroActivity::togglePause() {
  if (running_) {
    pauseRemainMs_ = remainingMs();
    running_ = false;
  } else {
    phaseEndMillis_ = millis() + pauseRemainMs_;
    running_ = true;
  }
  requestUpdate();
}

void PomodoroActivity::completePhase() {
  if (phase_ == Phase::Work) {
    ++todayCount_;
    ++totalCount_;
    saveStats();
    ++roundInCycle_;
    const bool longBreak = roundInCycle_ >= 4;
    if (longBreak) roundInCycle_ = 0;
    startPhase(longBreak ? Phase::LongBreak : Phase::ShortBreak, true);  // breaks run themselves
  } else {
    startPhase(Phase::Work, false);  // the next focus round waits for Confirm
  }
  requestUpdate(true);
}

void PomodoroActivity::skipPhase() {
  if (phase_ == Phase::Work) {
    // A skipped focus round does not count as completed.
    ++roundInCycle_;
    const bool longBreak = roundInCycle_ >= 4;
    if (longBreak) roundInCycle_ = 0;
    startPhase(longBreak ? Phase::LongBreak : Phase::ShortBreak, true);
  } else {
    startPhase(Phase::Work, false);
  }
  requestUpdate();
}

void PomodoroActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    togglePause();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (!running_ && remainingMs() == phaseDurationMs(phase_)) {
      // Preset switching only makes sense before a phase starts ticking.
      presetIndex_ = static_cast<uint8_t>((presetIndex_ + 1) % 3);
      startPhase(phase_, false);
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    skipPhase();
    return;
  }

  if (!running_) return;
  const uint32_t remain = remainingMs();
  if (remain == 0) {
    completePhase();
    return;
  }
  // Minute-granularity repaints, tightening to 10s inside the last minute —
  // keeps the e-ink update cadence low without the display going stale.
  const uint32_t step = (remain <= 60000U) ? 10000U : 60000U;
  if (lastShownRemainMs_ > remain && lastShownRemainMs_ - remain >= step) {
    lastShownRemainMs_ = remain;
    requestUpdate();
  }
}

void PomodoroActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect fullScreen{0, 0, sw, sh};

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_POMO_TITLE));

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;
  int y = contentTop + metrics.verticalSpacing * 2;

  const char* phaseText = phase_ == Phase::Work ? (running_ ? tr(STR_POMO_FOCUS) : tr(STR_POMO_READY))
                          : phase_ == Phase::ShortBreak ? tr(STR_POMO_SHORT_BREAK)
                                                        : tr(STR_POMO_LONG_BREAK);
  UITheme::drawCenteredText(renderer, fullScreen, UI_12_FONT_ID, y, phaseText, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing * 2;

  const uint32_t remain = remainingMs();
  char buf[64];
  snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(remain / 60000U),
           static_cast<unsigned>((remain / 1000U) % 60U));
  UITheme::drawCenteredText(renderer, fullScreen, NOTOSANS_18_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + metrics.verticalSpacing * 2;

  // Progress bar for the current phase.
  const uint32_t total = phaseDurationMs(phase_);
  const int barW = contentW;
  const int barH = 12;
  const int filled = total > 0 ? static_cast<int>(static_cast<uint64_t>(total - remain) * barW / total) : 0;
  renderer.drawRect(contentX, y, barW, barH, true);
  if (filled > 2) renderer.fillRect(contentX + 1, y + 1, filled - 2, barH - 2, true);
  y += barH + metrics.verticalSpacing * 2;

  // Cycle dots: four focus rounds per long-break cycle.
  const int dotR = 6;
  const int dotGap = 24;
  int dotX = sw / 2 - (dotGap * 3) / 2;
  for (int i = 0; i < 4; ++i) {
    if (i < roundInCycle_) {
      renderer.fillRect(dotX - dotR / 2, y, dotR, dotR, true);
    } else {
      renderer.drawRect(dotX - dotR / 2, y, dotR, dotR, true);
    }
    dotX += dotGap;
  }
  y += dotR + metrics.verticalSpacing * 3;

  snprintf(buf, sizeof(buf), "%s %u · %s %u", tr(STR_POMO_TODAY), todayCount_, tr(STR_POMO_TOTAL),
           static_cast<unsigned>(totalCount_));
  UITheme::drawCenteredText(renderer, fullScreen, UI_10_FONT_ID, y, buf);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  const Preset& p = kPresets[presetIndex_];
  snprintf(buf, sizeof(buf), "%s %u-%u-%u", tr(STR_POMO_PRESET), p.workMin, p.shortMin, p.longMin);
  UITheme::drawCenteredText(renderer, fullScreen, SMALL_FONT_ID, y, buf);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), running_ ? tr(STR_POMO_PAUSE) : tr(STR_POMO_START),
                                            tr(STR_POMO_PRESET), tr(STR_POMO_SKIP));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

uint32_t PomodoroActivity::todayYmd() const {
  const time_t now = time(nullptr);
  if (now < static_cast<time_t>(kMinValidEpoch)) return 0;
  struct tm tmLocal;
  localtime_r(&now, &tmLocal);
  return static_cast<uint32_t>((tmLocal.tm_year + 1900) * 10000 + (tmLocal.tm_mon + 1) * 100 + tmLocal.tm_mday);
}

void PomodoroActivity::loadStats() {
  StatsRecord rec;
  HalFile file;
  if (Storage.openFileForRead("POMO", kStatsPath, file) &&
      file.read(&rec, sizeof(rec)) == static_cast<int>(sizeof(rec)) && rec.magic == kStatsMagic) {
    totalCount_ = rec.total;
    statsYmd_ = todayYmd();
    todayCount_ = (rec.ymd == statsYmd_) ? rec.today : 0;
  } else {
    statsYmd_ = todayYmd();
  }
}

void PomodoroActivity::saveStats() const {
  StatsRecord rec;
  rec.ymd = statsYmd_;
  rec.today = todayCount_;
  rec.total = totalCount_;
  HalFile file;
  if (!Storage.openFileForWrite("POMO", kStatsPath, file)) {
    LOG_ERR("POMO", "stats open failed");
    return;
  }
  file.write(&rec, sizeof(rec));
}
