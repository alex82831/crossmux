#include "ExchangeRateActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/apps/netkit/NetKit.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kCachePath = "/.crosspoint/exchange.json";
constexpr const char* kApiUrl = "https://open.er-api.com/v6/latest/CNY";
// The full table lists ~160 currencies (~6KB); 12KB absorbs formatting drift.
constexpr size_t kMaxResponseBytes = 12288;
constexpr uint32_t kMinValidEpoch = 1600000000;

struct CurrencyDef {
  const char* code;
  const char* name;
};
constexpr CurrencyDef kCurrencies[8] = {
    {"USD", "美元"}, {"EUR", "欧元"}, {"JPY", "日元"},   {"GBP", "英镑"},
    {"HKD", "港币"}, {"KRW", "韩元"}, {"TWD", "新台币"}, {"AUD", "澳元"},
};

constexpr int kAmounts[4] = {1, 10, 100, 1000};
}  // namespace

void ExchangeRateActivity::onEnter() {
  Activity::onEnter();
  loadCache();
  requestUpdate();
}

void ExchangeRateActivity::onExit() {
  netkit::teardownWifi();
  Activity::onExit();
}

void ExchangeRateActivity::loop() {
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
    amountIndex_ = static_cast<uint8_t>((amountIndex_ + 3) % 4);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    amountIndex_ = static_cast<uint8_t>((amountIndex_ + 1) % 4);
    requestUpdate();
  }
}

void ExchangeRateActivity::refresh() {
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

void ExchangeRateActivity::doFetch() {
  fetching_ = true;
  requestUpdateAndWait();

  std::string body;
  const bool ok = netkit::fetchToString(kApiUrl, body, kMaxResponseBytes);
  fetching_ = false;

  if (ok && applyApiJson(body)) {
    status_ = Status::Ready;
    fetchedEpoch_ = static_cast<uint32_t>(time(nullptr));
    saveCache();
  } else {
    LOG_ERR("FX", "fetch failed (ok=%d, %u bytes)", ok, static_cast<unsigned>(body.size()));
    if (status_ != Status::Ready) status_ = Status::Failed;
  }
  requestUpdate();
}

bool ExchangeRateActivity::applyApiJson(const std::string& json) {
  // Transient parse of a ≤12KB body; the doc frees on scope exit. Only the
  // eight tracked currencies are copied out.
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  JsonObject rates = doc["rates"];
  if (rates.isNull()) return false;
  float parsed[kCurrencyCount];
  for (int i = 0; i < kCurrencyCount; ++i) {
    const float perCny = rates[kCurrencies[i].code] | 0.0f;  // 1 CNY = perCny units
    if (perCny <= 0.0f) return false;
    parsed[i] = 1.0f / perCny;  // 1 unit = parsed CNY
  }
  for (int i = 0; i < kCurrencyCount; ++i) rates_[i] = parsed[i];
  return true;
}

void ExchangeRateActivity::loadCache() {
  const String raw = Storage.readFile(kCachePath);
  if (raw.length() == 0) return;
  JsonDocument doc;
  if (deserializeJson(doc, raw.c_str()) != DeserializationError::Ok) return;
  fetchedEpoch_ = doc["t"] | 0U;
  bool allValid = true;
  for (int i = 0; i < kCurrencyCount; ++i) {
    rates_[i] = doc[kCurrencies[i].code] | 0.0f;
    if (rates_[i] <= 0.0f) allValid = false;
  }
  if (allValid) status_ = Status::Ready;
}

void ExchangeRateActivity::saveCache() const {
  JsonDocument doc;
  doc["t"] = fetchedEpoch_;
  for (int i = 0; i < kCurrencyCount; ++i) doc[kCurrencies[i].code] = rates_[i];
  String out;
  serializeJson(doc, out);
  Storage.writeFile(kCachePath, out);
}

void ExchangeRateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect fullScreen{0, 0, sw, renderer.getScreenHeight()};

  renderer.clearScreen();
  char subtitle[32];
  snprintf(subtitle, sizeof(subtitle), "%d %s", kAmounts[amountIndex_], tr(STR_FX_UNIT_FOREIGN));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_FX_TITLE), subtitle);

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;
  const int contentH = safe.y + safe.height - contentTop;

  if (fetching_) {
    UITheme::drawCenteredWrappedText(renderer, Rect{contentX, contentTop, contentW, contentH}, UI_12_FONT_ID,
                                     tr(STR_LOADING), 2);
  } else if (status_ != Status::Ready) {
    UITheme::drawCenteredWrappedText(renderer, Rect{contentX, contentTop, contentW, contentH}, UI_12_FONT_ID,
                                     status_ == Status::Failed ? tr(STR_APP_FETCH_FAILED) : tr(STR_FX_PROMPT), 3);
  } else {
    int y = contentTop + metrics.verticalSpacing;
    const int rowH = renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
    const int amount = kAmounts[amountIndex_];
    char buf[64];
    for (int i = 0; i < kCurrencyCount && y + rowH <= contentTop + contentH; ++i) {
      snprintf(buf, sizeof(buf), "%s %s", kCurrencies[i].name, kCurrencies[i].code);
      renderer.drawText(UI_12_FONT_ID, contentX, y, buf);
      const double cny = static_cast<double>(rates_[i]) * amount;
      snprintf(buf, sizeof(buf), (cny >= 1000.0) ? "%.0f %s" : "%.2f %s", cny, tr(STR_FX_CNY));
      renderer.drawText(UI_12_FONT_ID, contentX + contentW - renderer.getTextWidth(UI_12_FONT_ID, buf), y, buf,
                        true, EpdFontFamily::BOLD);
      y += rowH;
    }
    if (fetchedEpoch_ > kMinValidEpoch) {
      time_t t = static_cast<time_t>(fetchedEpoch_);
      struct tm tmLocal;
      localtime_r(&t, &tmLocal);
      snprintf(buf, sizeof(buf), "%s %02d-%02d %02d:%02d", tr(STR_WEATHER_UPDATED), tmLocal.tm_mon + 1,
               tmLocal.tm_mday, tmLocal.tm_hour, tmLocal.tm_min);
      UITheme::drawCenteredText(renderer, fullScreen, SMALL_FONT_ID, y + metrics.verticalSpacing, buf);
    }
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_APP_REFRESH), tr(STR_FX_AMOUNT), tr(STR_FX_AMOUNT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
