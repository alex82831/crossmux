#pragma once

#include <string>

#include "activities/Activity.h"

// Daily Chinese poem (每日诗词): fetches one classical poem line from the
// jinrishici open API on demand and keeps the last one cached on SD so the
// screen still shows a poem offline. CN-build only (ENABLE_CHINESE_VERSION).
class PoemActivity final : public Activity {
 public:
  PoemActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Poem", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Status : uint8_t { Empty, Ready, Failed };

  void refresh();
  void doFetch();
  bool applyJson(const std::string& json);
  void loadCache();

  // Poem text tops out around a few hundred bytes; strings live for the
  // activity's lifetime and are replaced wholesale on refresh.
  std::string content_;
  std::string origin_;
  std::string author_;
  Status status_ = Status::Empty;
  bool fetching_ = false;
};
