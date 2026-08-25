#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Weather (天气): current conditions plus a 3-day outlook for a built-in list
// of Chinese cities, fetched on demand from the free Open-Meteo API. The last
// response is cached on SD so the screen still shows data offline. CN-build
// only (ENABLE_CHINESE_VERSION).
class WeatherActivity final : public Activity {
 public:
  WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Status : uint8_t { Empty, Ready, Failed };

  struct DayForecast {
    int code = 0;
    float tMin = 0;
    float tMax = 0;
    int precipProb = 0;  // daily max precipitation probability, %
    int weekday = -1;    // 0 = Sunday, -1 = unknown
  };
  static constexpr int kForecastDays = 5;

  void refresh();
  void doFetch();
  bool applyJson(const std::string& json);
  void cycleCity(int step);
  void loadState();
  void saveState() const;
  static const char* conditionText(int wmoCode);
  static int weekdayOf(const char* isoDate);
  const char* dayLabel(int index, char* buf, size_t bufLen) const;

  int cityIndex_ = 0;
  float currentTemp_ = 0;
  float feelsLike_ = 0;
  int humidity_ = 0;
  float windKmh_ = 0;
  int currentCode_ = 0;
  DayForecast days_[kForecastDays];
  uint32_t fetchedEpoch_ = 0;
  Status status_ = Status::Empty;
  bool fetching_ = false;
};
