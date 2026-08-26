#pragma once

#include <cstdint>

#include "StandbyFace.h"

// "Lines" — a standby face that shows one line from a text file on the card,
// and a different one next time.
//
// The point is the medium: an e-ink panel holds its image for free while the
// device is closed, so whatever is on it is being read, passively, all day.
// A vocabulary list, a set of phrases, lines you want to memorise, a packing
// list — one entry per line in /standby/lines.txt and the device becomes a
// slow drip of that.
//
// The file is never held in RAM. Lines are located by streaming the file, so
// the list can be as long as the card allows; only the chosen line is kept.
class QuoteFace final : public StandbyFace {
 public:
  void onEnter() override;
  bool tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override;
  uint32_t secondsUntilNextWake() const override;

  // Up / Down step through the list by hand; a shake jumps somewhere random.
  void onPagePrev() override;
  void onPageNext() override;
  void onShake(uint32_t seed) override;

  // Long, quiet, text-only page — the same case the almanac face makes for a
  // grayscale pass.
  bool wantsGrayscale() const override { return true; }

 private:
  static constexpr int kMaxLine = 512;

  uint32_t index_ = 0;
  uint32_t lineCount_ = 0;
  uint32_t lastRotateMinute_ = 0;
  bool dirty_ = true;
  char line_[kMaxLine] = {};

  // Load line `index_`, wrapping if the file turned out to be shorter.
  void load();
};
