#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "activities/apps/netkit/AppAbout.h"

// 番茄钟 (Pomodoro focus timer): work/short-break cycles with a long break
// after four rounds, three duration presets, minute-granularity e-ink
// updates, and persisted daily/total completion counts. CN-build only
// (ENABLE_CHINESE_VERSION).
class PomodoroActivity final : public Activity {
 public:
  PomodoroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Pomodoro", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return running_; }

 private:
  enum class Phase : uint8_t { Work, ShortBreak, LongBreak };

  struct Preset {
    uint8_t workMin;
    uint8_t shortMin;
    uint8_t longMin;
  };

  void startPhase(Phase phase, bool autoRun);
  void togglePause();
  void skipPhase();
  void completePhase();
  uint32_t phaseDurationMs(Phase phase) const;
  uint32_t remainingMs() const;
  void loadStats();
  void saveStats() const;
  uint32_t todayYmd() const;

  static constexpr Preset kPresets[3] = {{25, 5, 15}, {30, 5, 20}, {45, 10, 20}};

  Phase phase_ = Phase::Work;
  bool running_ = false;
  uint8_t presetIndex_ = 0;
  uint8_t roundInCycle_ = 0;  // completed work phases in the current 4-cycle
  uint32_t phaseEndMillis_ = 0;
  uint32_t pauseRemainMs_ = 0;
  uint32_t lastShownRemainMs_ = 0;
  uint16_t todayCount_ = 0;
  uint32_t totalCount_ = 0;
  uint32_t statsYmd_ = 0;

  appabout::AboutGate aboutGate_;
};
