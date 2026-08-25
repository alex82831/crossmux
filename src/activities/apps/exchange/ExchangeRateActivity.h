#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

// 汇率 (Exchange rates): CNY against eight major currencies from the free
// open.er-api.com feed, with 1/10/100/1000 amount presets and an SD cache so
// the last table stays readable offline. CN-build only
// (ENABLE_CHINESE_VERSION).
class ExchangeRateActivity final : public Activity {
 public:
  ExchangeRateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Exchange", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kCurrencyCount = 8;

  enum class Status : uint8_t { Empty, Ready, Failed };

  void refresh();
  void doFetch();
  bool applyApiJson(const std::string& json);
  void loadCache();
  void saveCache() const;

  // 1 foreign unit = rate_[i] CNY.
  float rates_[kCurrencyCount] = {};
  uint32_t fetchedEpoch_ = 0;
  uint8_t amountIndex_ = 0;
  Status status_ = Status::Empty;
  bool fetching_ = false;
};
