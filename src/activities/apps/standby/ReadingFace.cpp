#include "ReadingFace.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "StandbyTime.h"
#include "fontIds.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {

constexpr int kBarHeight = 14;

// "42" + a unit, rather than "0:42" — on a lock screen the number is what the
// eye lands on, and minutes are the unit people think in until it passes an
// hour.
void formatDuration(const uint64_t ms, char* out, const size_t cap) {
  const uint32_t totalMinutes = static_cast<uint32_t>(ms / 60000ULL);
  if (totalMinutes < 60) {
    snprintf(out, cap, "%u", static_cast<unsigned>(totalMinutes));
  } else {
    snprintf(out, cap, "%u:%02u", static_cast<unsigned>(totalMinutes / 60), static_cast<unsigned>(totalMinutes % 60));
  }
}

const char* durationUnit(const uint64_t ms) {
  return ms / 60000ULL < 60 ? tr(STR_STANDBY_UNIT_MINUTES) : tr(STR_STANDBY_UNIT_HOURS);
}

}  // namespace

void ReadingFace::onEnter() { refresh(); }

void ReadingFace::refresh() {
  const uint32_t epoch = TimeUtils::getCurrentValidTimestamp();
  dayOrdinal_ = epoch != 0 ? TimeUtils::getLocalDayOrdinal(epoch) : 0;

  todayMs_ = dayOrdinal_ != 0 ? ReadingStatsAnalytics::buildTimelineDayEntry(dayOrdinal_).totalReadingMs : 0;
  goalMs_ = SETTINGS.getDailyGoalMs();
  streakDays_ = ACHIEVEMENTS.getCurrentGoalStreak();

  const auto& books = RECENT_BOOKS.getBooks();
  haveBook_ = !books.empty();
  if (haveBook_) {
    snprintf(bookTitle_, sizeof(bookTitle_), "%s", books.front().title.c_str());
  } else {
    bookTitle_[0] = '\0';
  }
  lastMinuteTick_ = standby_time::getMinuteTick(0);
}

bool ReadingFace::tick() {
  // Once a minute is as often as any of these figures can move, and it is also
  // the fastest an e-ink refresh is worth spending here.
  const uint32_t minute = standby_time::getMinuteTick(0);
  if (minute == lastMinuteTick_) return false;
  refresh();
  return true;
}

StrId ReadingFace::titleId() const { return StrId::STR_STANDBY_FACE_READING; }

// Everything shown here has minute resolution, so there is nothing to gain
// from waking sooner.
uint32_t ReadingFace::secondsUntilNextWake() const { return 60; }

void ReadingFace::render(GfxRenderer& renderer, const Rect& viewport) {
  const int cx = viewport.x + viewport.width / 2;
  const int vh = viewport.height;
  int y = viewport.y + vh / 6;

  // Hero: minutes read today.
  char value[16];
  formatDuration(todayMs_, value, sizeof(value));
  const char* unit = durationUnit(todayMs_);

  const int valueW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, value);
  const int unitW = renderer.getTextWidth(UI_12_FONT_ID, unit);
  const int heroW = valueW + 6 + unitW;
  renderer.drawText(NOTOSANS_18_FONT_ID, cx - heroW / 2, y, value);
  renderer.drawText(UI_12_FONT_ID, cx - heroW / 2 + valueW + 6, y + renderer.getLineHeight(NOTOSANS_18_FONT_ID) -
                                                                    renderer.getLineHeight(UI_12_FONT_ID) - 2,
                    unit);
  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 4;

  renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_STANDBY_TODAY_READ));
  y += renderer.getLineHeight(SMALL_FONT_ID) + 18;

  // Progress toward the daily goal. Drawn even at 0% so the bar is a constant
  // landmark rather than something that appears once you have read.
  const int barW = viewport.width * 2 / 3;
  const int barX = cx - barW / 2;
  renderer.drawRect(barX, y, barW, kBarHeight, 2, true);
  if (goalMs_ > 0) {
    uint64_t filled = todayMs_ * static_cast<uint64_t>(barW - 4) / goalMs_;
    if (filled > static_cast<uint64_t>(barW - 4)) filled = static_cast<uint64_t>(barW - 4);
    if (filled > 0) renderer.fillRect(barX + 2, y + 2, static_cast<int>(filled), kBarHeight - 4, true);
  }
  y += kBarHeight + 8;

  char goalLine[64];
  char goalValue[16];
  formatDuration(goalMs_, goalValue, sizeof(goalValue));
  snprintf(goalLine, sizeof(goalLine), "%s %s %s", tr(STR_STANDBY_GOAL), goalValue, durationUnit(goalMs_));
  renderer.drawCenteredText(SMALL_FONT_ID, y, goalLine);
  y += renderer.getLineHeight(SMALL_FONT_ID) + 22;

  // Streak: the number worth protecting.
  if (streakDays_ > 0) {
    char streak[64];
    snprintf(streak, sizeof(streak), "%s %u %s", tr(STR_STANDBY_STREAK), static_cast<unsigned>(streakDays_),
             tr(STR_STANDBY_UNIT_DAYS));
    renderer.drawCenteredText(UI_12_FONT_ID, y, streak);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
  }

  // What is open, at the foot of the page.
  const int footY = viewport.y + vh - renderer.getLineHeight(SMALL_FONT_ID) - 12;
  if (haveBook_) {
    const std::string title = renderer.truncatedText(SMALL_FONT_ID, bookTitle_, viewport.width - 48);
    renderer.drawCenteredText(SMALL_FONT_ID, footY, title.c_str());
  } else {
    renderer.drawCenteredText(SMALL_FONT_ID, footY, tr(STR_STANDBY_NO_BOOK));
  }
}
