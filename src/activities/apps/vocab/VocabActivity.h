#pragma once

#include <cstdint>

#include "activities/Activity.h"

// 单词卡 (Flashcards): built-in high-frequency CET-4 deck with a three-box
// Leitner flow — unknown words come back often, mastered ones rarely.
// Confirm reveals the gloss, Up = knew it, Down = didn't. Per-word progress
// persists on SD. CN-build only (ENABLE_CHINESE_VERSION).
class VocabActivity final : public Activity {
 public:
  VocabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Vocab", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void pickNext();
  void grade(bool knew);
  void countBoxes(int& fresh, int& learning, int& mastered) const;
  void loadProgress();
  void saveProgress() const;

  // One Leitner box (0..2) per word; ~200 bytes inside the heap-allocated
  // activity, mirrored to SD on grade.
  uint8_t box_[512] = {};
  int current_ = 0;
  int previous_ = -1;
  bool revealed_ = false;
  uint16_t sessionSeen_ = 0;
  bool dirty_ = false;
};
