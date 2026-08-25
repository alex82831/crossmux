#include "PoemActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/apps/netkit/NetKit.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kCachePath = "/.crosspoint/poem.json";
constexpr const char* kApiUrl = "https://v1.jinrishici.com/all.json";
// One poem line plus origin/author; 2KB tolerates the API's occasional
// full-poem payloads without letting a misbehaving server grow the heap.
constexpr size_t kMaxResponseBytes = 2048;
}  // namespace

void PoemActivity::onEnter() {
  Activity::onEnter();
  loadCache();
  requestUpdate();
}

void PoemActivity::onExit() {
  // This screen is the only Wi-Fi consumer in its flow; drop the link so the
  // Apps menu never keeps the radio powered.
  netkit::teardownWifi();
  Activity::onExit();
}

void PoemActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    refresh();
  }
}

void PoemActivity::refresh() {
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

void PoemActivity::doFetch() {
  fetching_ = true;
  requestUpdateAndWait();  // paint the loading frame before the blocking GET

  std::string body;
  const bool ok = netkit::fetchToString(kApiUrl, body, kMaxResponseBytes);
  fetching_ = false;

  if (ok && applyJson(body)) {
    status_ = Status::Ready;
    Storage.writeFile(kCachePath, String(body.c_str()));
  } else {
    LOG_ERR("POEM", "fetch failed (ok=%d, %u bytes)", ok, static_cast<unsigned>(body.size()));
    // Keep any previously shown poem; only flag the failure.
    status_ = (status_ == Status::Ready) ? Status::Ready : Status::Failed;
  }
  requestUpdate();
}

bool PoemActivity::applyJson(const std::string& json) {
  // Transient parse of a ≤2KB body; ArduinoJson's pools are freed when doc
  // leaves scope, so nothing is held across the activity loop.
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  const char* content = doc["content"];
  if (content == nullptr || content[0] == '\0') return false;
  content_ = content;
  origin_ = doc["origin"] | "";
  author_ = doc["author"] | "";
  return true;
}

void PoemActivity::loadCache() {
  const String raw = Storage.readFile(kCachePath);
  if (raw.length() == 0) return;
  if (applyJson(std::string(raw.c_str()))) status_ = Status::Ready;
}

void PoemActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_POEM_TITLE));

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;
  const int contentH = safe.y + safe.height - contentTop;

  if (fetching_) {
    UITheme::drawCenteredWrappedText(renderer, Rect{contentX, contentTop, contentW, contentH}, UI_12_FONT_ID,
                                     tr(STR_LOADING), 2);
  } else if (content_.empty()) {
    UITheme::drawCenteredWrappedText(renderer, Rect{contentX, contentTop, contentW, contentH}, UI_12_FONT_ID,
                                     status_ == Status::Failed ? tr(STR_APP_FETCH_FAILED) : tr(STR_POEM_PROMPT), 3);
  } else {
    // Poem body in the upper band, attribution in the lower quarter.
    const int attributionH = renderer.getLineHeight(UI_10_FONT_ID) * 2;
    const Rect bodyRect{contentX, contentTop, contentW, contentH - attributionH - metrics.verticalSpacing};
    UITheme::drawCenteredWrappedText(renderer, bodyRect, UI_12_FONT_ID, content_.c_str(), 10, true,
                                     EpdFontFamily::BOLD);

    char attribution[192];
    if (!origin_.empty() && !author_.empty()) {
      snprintf(attribution, sizeof(attribution), "\xE3\x80\x8C%s\xE3\x80\x8D %s", origin_.c_str(), author_.c_str());
    } else {
      snprintf(attribution, sizeof(attribution), "%s%s", origin_.c_str(), author_.c_str());
    }
    const Rect attrRect{contentX, bodyRect.y + bodyRect.height + metrics.verticalSpacing, contentW, attributionH};
    UITheme::drawCenteredWrappedText(renderer, attrRect, UI_10_FONT_ID, attribution, 2);

    if (status_ == Status::Failed) {
      UITheme::drawCenteredText(renderer, Rect{0, 0, sw, renderer.getScreenHeight()}, UI_10_FONT_ID,
                                contentTop + contentH - renderer.getLineHeight(UI_10_FONT_ID),
                                tr(STR_APP_FETCH_FAILED));
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_APP_REFRESH), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
