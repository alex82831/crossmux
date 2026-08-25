#pragma once

#include <cstdint>

#include "activities/Activity.h"

// 三国霸业 launcher: continue a saved campaign, start a new one (faction
// pick), or read the rules. CN-build only (ENABLE_CHINESE_VERSION).
class SanguoMenuActivity final : public Activity {
 public:
  SanguoMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SanguoMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Main, FactionPick, Help };

  int mainItemCount() const;

  View view_ = View::Main;
  bool hasSave_ = false;
  uint8_t selected_ = 0;
};
