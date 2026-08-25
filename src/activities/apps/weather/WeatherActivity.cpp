#include "WeatherActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "WeatherCities.h"
#include "activities/ActivityManager.h"
#include "activities/apps/netkit/NetKit.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kStateDir = "/.crosspoint/weather";
constexpr const char* kStatePath = "/.crosspoint/weather/state";
constexpr const char* kCachePath = "/.crosspoint/weather/last.json";
// Open-Meteo answers this query with ~700 bytes; 4KB absorbs formatting drift
// without letting a misbehaving server grow the heap.
constexpr size_t kMaxResponseBytes = 4096;
constexpr uint32_t kMinValidEpoch = 1600000000;  // RTC unset guard (2020-09)
}  // namespace

void WeatherActivity::onEnter() {
  Activity::onEnter();
  loadState();
  requestUpdate();
}

void WeatherActivity::onExit() {
  // Sole Wi-Fi consumer in this flow: power the radio down on the way out.
  netkit::teardownWifi();
  Activity::onExit();
}

void WeatherActivity::loop() {
  bool aboutRepaint = false;
  if (aboutGate_.handle(mappedInput, aboutRepaint)) {
    if (aboutRepaint) requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    refresh();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cycleCity(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cycleCity(1);
  }
}

void WeatherActivity::cycleCity(const int step) {
  cityIndex_ = (cityIndex_ + step + kWeatherCityCount) % kWeatherCityCount;
  // The old forecast belongs to another city; show the fetch prompt instead.
  status_ = Status::Empty;
  saveState();
  requestUpdate();
}

void WeatherActivity::refresh() {
  if (netkit::wifiConnected()) {
    doFetch();
    return;
  }
  startActivityForResultWith<WifiSelectionActivity>(
      [this](const ActivityResult& result) {
        if (!result.isCancelled && netkit::wifiConnected()) doFetch();
      },
      true);
}

void WeatherActivity::doFetch() {
  fetching_ = true;
  requestUpdateAndWait();  // paint the loading frame before the blocking GET

  const WeatherCity& city = kWeatherCities[cityIndex_];
  char url[384];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&timezone=Asia%%2FShanghai&forecast_days=%d",
           static_cast<double>(city.lat), static_cast<double>(city.lon), kForecastDays);

  std::string body;
  const bool ok = netkit::fetchToString(url, body, kMaxResponseBytes);
  fetching_ = false;

  if (ok && applyJson(body)) {
    status_ = Status::Ready;
    fetchedEpoch_ = static_cast<uint32_t>(time(nullptr));
    Storage.ensureDirectoryExists(kStateDir);
    Storage.writeFile(kCachePath, String(body.c_str()));
    saveState();
  } else {
    LOG_ERR("WTHR", "fetch failed (ok=%d, %u bytes)", ok, static_cast<unsigned>(body.size()));
    status_ = Status::Failed;
  }
  requestUpdate();
}

bool WeatherActivity::applyJson(const std::string& json) {
  // Transient parse of a ≤4KB body; the document is freed on scope exit.
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  JsonObject current = doc["current"];
  JsonObject daily = doc["daily"];
  if (current.isNull() || daily.isNull()) return false;

  currentTemp_ = current["temperature_2m"] | 0.0f;
  feelsLike_ = current["apparent_temperature"] | currentTemp_;
  humidity_ = current["relative_humidity_2m"] | 0;
  windKmh_ = current["wind_speed_10m"] | 0.0f;
  currentCode_ = current["weather_code"] | 0;

  JsonArray codes = daily["weather_code"];
  JsonArray tMax = daily["temperature_2m_max"];
  JsonArray tMin = daily["temperature_2m_min"];
  JsonArray precip = daily["precipitation_probability_max"];
  JsonArray dates = daily["time"];
  for (int i = 0; i < kForecastDays; ++i) {
    days_[i].code = (i < static_cast<int>(codes.size())) ? codes[i].as<int>() : 0;
    days_[i].tMax = (i < static_cast<int>(tMax.size())) ? tMax[i].as<float>() : 0.0f;
    days_[i].tMin = (i < static_cast<int>(tMin.size())) ? tMin[i].as<float>() : 0.0f;
    days_[i].precipProb = (i < static_cast<int>(precip.size())) ? precip[i].as<int>() : 0;
    days_[i].weekday = (i < static_cast<int>(dates.size())) ? weekdayOf(dates[i].as<const char*>()) : -1;
  }
  return true;
}

const char* WeatherActivity::dayLabel(const int index, char* buf, const size_t bufLen) const {
  switch (index) {
    case 0:
      return tr(STR_WEATHER_TODAY);
    case 1:
      return tr(STR_WEATHER_TOMORROW);
    case 2:
      return tr(STR_WEATHER_DAY_AFTER);
    default:
      break;
  }
  static constexpr StrId kWeekdays[7] = {StrId::STR_WEEK_SUN, StrId::STR_WEEK_MON, StrId::STR_WEEK_TUE,
                                         StrId::STR_WEEK_WED, StrId::STR_WEEK_THU, StrId::STR_WEEK_FRI,
                                         StrId::STR_WEEK_SAT};
  const int wd = days_[index].weekday;
  if (wd >= 0 && wd < 7) return I18N.get(kWeekdays[wd]);
  snprintf(buf, bufLen, "+%d", index);
  return buf;
}

// Sakamoto's day-of-week from an ISO "YYYY-MM-DD" date; -1 when unparsable.
int WeatherActivity::weekdayOf(const char* isoDate) {
  int y = 0, m = 0, d = 0;
  if (isoDate == nullptr || sscanf(isoDate, "%d-%d-%d", &y, &m, &d) != 3) return -1;
  static constexpr int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void WeatherActivity::loadState() {
  const String raw = Storage.readFile(kStatePath);
  int cachedCity = -1;
  if (raw.length() > 0) {
    int city = 0;
    unsigned epoch = 0;
    int cached = -1;
    if (sscanf(raw.c_str(), "%d %u %d", &city, &epoch, &cached) >= 1) {
      cityIndex_ = ((city % kWeatherCityCount) + kWeatherCityCount) % kWeatherCityCount;
      fetchedEpoch_ = epoch;
      cachedCity = cached;
    }
  }
  if (cachedCity == cityIndex_) {
    const String json = Storage.readFile(kCachePath);
    if (json.length() > 0 && applyJson(std::string(json.c_str()))) status_ = Status::Ready;
  }
}

void WeatherActivity::saveState() const {
  Storage.ensureDirectoryExists(kStateDir);
  char buf[48];
  const int cachedCity = (status_ == Status::Ready) ? cityIndex_ : -1;
  snprintf(buf, sizeof(buf), "%d %u %d", cityIndex_, static_cast<unsigned>(fetchedEpoch_), cachedCity);
  Storage.writeFile(kStatePath, String(buf));
}

const char* WeatherActivity::conditionText(const int wmoCode) {
  switch (wmoCode) {
    case 0:
      return tr(STR_WX_CLEAR);
    case 1:
      return tr(STR_WX_MOSTLY_CLEAR);
    case 2:
      return tr(STR_WX_PARTLY_CLOUDY);
    case 3:
      return tr(STR_WX_OVERCAST);
    case 45:
    case 48:
      return tr(STR_WX_FOG);
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return tr(STR_WX_DRIZZLE);
    case 61:
      return tr(STR_WX_RAIN_LIGHT);
    case 63:
      return tr(STR_WX_RAIN);
    case 65:
      return tr(STR_WX_RAIN_HEAVY);
    case 66:
    case 67:
      return tr(STR_WX_FREEZING_RAIN);
    case 71:
      return tr(STR_WX_SNOW_LIGHT);
    case 73:
      return tr(STR_WX_SNOW);
    case 75:
    case 77:
      return tr(STR_WX_SNOW_HEAVY);
    case 80:
    case 81:
    case 82:
      return tr(STR_WX_SHOWERS);
    case 85:
    case 86:
      return tr(STR_WX_SNOW_SHOWERS);
    case 95:
    case 96:
    case 99:
      return tr(STR_WX_THUNDERSTORM);
    default:
      return tr(STR_WX_UNKNOWN);
  }
}

void WeatherActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_WEATHER_TITLE),
                 kWeatherCities[cityIndex_].name);

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;
  const int contentH = safe.y + safe.height - contentTop;
  const Rect contentRect{contentX, contentTop, contentW, contentH};
  const Rect fullScreen{0, 0, sw, sh};

  if (fetching_) {
    UITheme::drawCenteredWrappedText(renderer, contentRect, UI_12_FONT_ID, tr(STR_LOADING), 2);
  } else if (status_ != Status::Ready) {
    UITheme::drawCenteredWrappedText(renderer, contentRect, UI_12_FONT_ID,
                                     status_ == Status::Failed ? tr(STR_APP_FETCH_FAILED) : tr(STR_WEATHER_PROMPT), 3);
  } else {
    char buf[160];
    int y = contentTop + metrics.verticalSpacing;

    // Current temperature headline at the biggest built-in size.
    snprintf(buf, sizeof(buf),
             "%.0f\xC2\xB0"
             "C",
             static_cast<double>(currentTemp_));
    UITheme::drawCenteredText(renderer, fullScreen, NOTOSANS_18_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + metrics.verticalSpacing * 2;

    UITheme::drawCenteredText(renderer, fullScreen, UI_12_FONT_ID, y, conditionText(currentCode_), true,
                              EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

    snprintf(buf, sizeof(buf), "%s %.0f\xC2\xB0 \xC2\xB7 %s %d%% \xC2\xB7 %s %.0f km/h", tr(STR_WEATHER_FEELS),
             static_cast<double>(feelsLike_), tr(STR_WEATHER_HUMIDITY), humidity_, tr(STR_WEATHER_WIND),
             static_cast<double>(windKmh_));
    UITheme::drawCenteredText(renderer, fullScreen, UI_10_FONT_ID, y, buf);
    y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

    if (fetchedEpoch_ > kMinValidEpoch) {
      time_t t = static_cast<time_t>(fetchedEpoch_);
      struct tm tmLocal;
      localtime_r(&t, &tmLocal);
      snprintf(buf, sizeof(buf), "%s %02d:%02d", tr(STR_WEATHER_UPDATED), tmLocal.tm_hour, tmLocal.tm_min);
      UITheme::drawCenteredText(renderer, fullScreen, SMALL_FONT_ID, y, buf);
      y += renderer.getLineHeight(SMALL_FONT_ID);
    }
    y += metrics.verticalSpacing * 2;

    // 5-day outlook: day label left, condition after it, rain% + min~max right.
    const int rowH = renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    for (int i = 0; i < kForecastDays && y + rowH <= contentTop + contentH; ++i) {
      char labelBuf[24];
      renderer.drawText(UI_10_FONT_ID, contentX, y, dayLabel(i, labelBuf, sizeof(labelBuf)));
      const char* cond = conditionText(days_[i].code);
      renderer.drawText(UI_10_FONT_ID, contentX + contentW * 30 / 100, y, cond);
      snprintf(buf, sizeof(buf), "%d%%  %.0f\xC2\xB0~%.0f\xC2\xB0", days_[i].precipProb,
               static_cast<double>(days_[i].tMin), static_cast<double>(days_[i].tMax));
      renderer.drawText(UI_10_FONT_ID, contentX + contentW - renderer.getTextWidth(UI_10_FONT_ID, buf), y, buf);
      y += rowH;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_APP_REFRESH), tr(STR_APP_CITY), tr(STR_APP_CITY));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (aboutGate_.open) appabout::drawOverlay(renderer, tr(STR_WEATHER_TITLE));
  renderer.displayBuffer();
}
