#pragma once

#include <cstdint>

#include "StandbyFace.h"

// "Reading" — the standby face that answers the question an e-reader's lock
// screen should answer: did I read today, and am I still on a run?
//
// Everything comes from data the reader already keeps (ReadingStatsAnalytics
// for today's total, AchievementsStore for the streak, RecentBooksStore for
// what is open), so it costs no network, no configuration, and nothing extra
// on the card. It redraws at most once a minute — on an e-ink panel the image
// holds itself, so the only cost is the refresh when the minute actually
// changes.
class ReadingFace final : public StandbyFace {
 public:
  void onEnter() override;
  bool tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override;
  uint32_t secondsUntilNextWake() const override;

  // Nothing to paginate; a shake just forces a refresh of the figures.
  void onPagePrev() override { refresh(); }
  void onPageNext() override { refresh(); }
  void onShake(uint32_t) override { refresh(); }

 private:
  uint64_t todayMs_ = 0;
  uint64_t goalMs_ = 0;
  uint32_t streakDays_ = 0;
  uint32_t dayOrdinal_ = 0;
  // Minute the figures were last gathered on, so tick() can tell a real change
  // from a poll.
  uint32_t lastMinuteTick_ = 0;
  bool haveBook_ = false;
  char bookTitle_[64] = {};

  void refresh();
};
