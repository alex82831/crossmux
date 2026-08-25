#pragma once

#include <string>

#include "activities/Activity.h"

// Daily Chinese poem (每日诗词): fetches one classical poem line from the
// jinrishici open API on demand and keeps a rolling history of the last 20
// on SD, browsable offline with Left/Right. CN-build only
// (ENABLE_CHINESE_VERSION).
class PoemActivity final : public Activity {
 public:
  PoemActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Poem", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kMaxHistory = 20;

  enum class Status : uint8_t { Empty, Ready, Failed };

  struct Poem {
    std::string content;
    std::string origin;
    std::string author;
  };

  void refresh();
  void doFetch();
  bool parsePoem(const std::string& json, Poem& out) const;
  void loadHistory();
  void saveHistory() const;
  void browse(int step);

  // Rolling history: ≤20 poems × a few hundred bytes ≈ 8KB of strings for
  // offline browsing; freed with the activity.
  Poem history_[kMaxHistory];
  int count_ = 0;
  int index_ = 0;  // 0 = newest
  Status status_ = Status::Empty;
  bool fetching_ = false;
};
