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
constexpr const char* kHistoryPath = "/.crosspoint/poems.json";
constexpr const char* kLegacyPath = "/.crosspoint/poem.json";
constexpr const char* kApiUrl = "https://v1.jinrishici.com/all.json";
// One poem line plus origin/author; 2KB tolerates the API's occasional
// full-poem payloads without letting a misbehaving server grow the heap.
constexpr size_t kMaxResponseBytes = 2048;
}  // namespace

void PoemActivity::onEnter() {
  Activity::onEnter();
  loadHistory();
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
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    browse(1);  // towards older entries
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    browse(-1);
  }
}

void PoemActivity::browse(const int step) {
  if (count_ <= 1) return;
  const int next = index_ + step;
  if (next < 0 || next >= count_) return;
  index_ = next;
  requestUpdate();
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

  Poem fetched;
  if (ok && parsePoem(body, fetched)) {
    // Push front, trim the tail.
    if (count_ < kMaxHistory) ++count_;
    for (int i = count_ - 1; i > 0; --i) history_[i] = history_[i - 1];
    history_[0] = fetched;
    index_ = 0;
    status_ = Status::Ready;
    saveHistory();
  } else {
    LOG_ERR("POEM", "fetch failed (ok=%d, %u bytes)", ok, static_cast<unsigned>(body.size()));
    status_ = (count_ > 0) ? Status::Ready : Status::Failed;
  }
  requestUpdate();
}

bool PoemActivity::parsePoem(const std::string& json, Poem& out) const {
  // Transient parse of a ≤2KB body; freed when doc leaves scope.
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  const char* content = doc["content"];
  if (content == nullptr || content[0] == '\0') return false;
  out.content = content;
  out.origin = doc["origin"] | "";
  out.author = doc["author"] | "";
  return true;
}

void PoemActivity::loadHistory() {
  count_ = 0;
  index_ = 0;
  String raw = Storage.readFile(kHistoryPath);
  if (raw.length() == 0) {
    // One-time migration from the single-poem cache of the first release.
    const String legacy = Storage.readFile(kLegacyPath);
    if (legacy.length() > 0) {
      Poem p;
      if (parsePoem(std::string(legacy.c_str()), p)) {
        history_[0] = p;
        count_ = 1;
        status_ = Status::Ready;
        saveHistory();
      }
      Storage.remove(kLegacyPath);
    }
    return;
  }
  if (raw.length() > 16384) return;  // corrupt/oversized: start fresh
  JsonDocument doc;
  if (deserializeJson(doc, raw.c_str()) != DeserializationError::Ok) return;
  for (JsonObject entry : doc.as<JsonArray>()) {
    if (count_ >= kMaxHistory) break;
    const char* content = entry["c"];
    if (content == nullptr || content[0] == '\0') continue;
    history_[count_].content = content;
    history_[count_].origin = entry["o"] | "";
    history_[count_].author = entry["a"] | "";
    ++count_;
  }
  if (count_ > 0) status_ = Status::Ready;
}

void PoemActivity::saveHistory() const {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count_; ++i) {
    JsonObject entry = arr.add<JsonObject>();
    entry["c"] = history_[i].content;
    entry["o"] = history_[i].origin;
    entry["a"] = history_[i].author;
  }
  String out;
  serializeJson(doc, out);
  Storage.writeFile(kHistoryPath, out);
}

void PoemActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();
  char subtitle[24] = {};
  if (count_ > 1) snprintf(subtitle, sizeof(subtitle), "%d/%d", index_ + 1, count_);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_POEM_TITLE),
                 subtitle[0] != '\0' ? subtitle : nullptr);

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;
  const int contentH = safe.y + safe.height - contentTop;

  if (fetching_) {
    UITheme::drawCenteredWrappedText(renderer, Rect{contentX, contentTop, contentW, contentH}, UI_12_FONT_ID,
                                     tr(STR_LOADING), 2);
  } else if (count_ == 0) {
    UITheme::drawCenteredWrappedText(renderer, Rect{contentX, contentTop, contentW, contentH}, UI_12_FONT_ID,
                                     status_ == Status::Failed ? tr(STR_APP_FETCH_FAILED) : tr(STR_POEM_PROMPT), 3);
  } else {
    const Poem& poem = history_[index_];
    // Poem body in the upper band, attribution in the lower quarter.
    const int attributionH = renderer.getLineHeight(UI_10_FONT_ID) * 2;
    const Rect bodyRect{contentX, contentTop, contentW, contentH - attributionH - metrics.verticalSpacing};
    UITheme::drawCenteredWrappedText(renderer, bodyRect, UI_12_FONT_ID, poem.content.c_str(), 10, true,
                                     EpdFontFamily::BOLD);

    char attribution[192];
    if (!poem.origin.empty() && !poem.author.empty()) {
      snprintf(attribution, sizeof(attribution), "\xE3\x80\x8C%s\xE3\x80\x8D %s", poem.origin.c_str(),
               poem.author.c_str());
    } else {
      snprintf(attribution, sizeof(attribution), "%s%s", poem.origin.c_str(), poem.author.c_str());
    }
    const Rect attrRect{contentX, bodyRect.y + bodyRect.height + metrics.verticalSpacing, contentW, attributionH};
    UITheme::drawCenteredWrappedText(renderer, attrRect, UI_10_FONT_ID, attribution, 2);

    if (status_ == Status::Failed) {
      UITheme::drawCenteredText(renderer, Rect{0, 0, sw, renderer.getScreenHeight()}, UI_10_FONT_ID,
                                contentTop + contentH - renderer.getLineHeight(UI_10_FONT_ID),
                                tr(STR_APP_FETCH_FAILED));
    }
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_APP_REFRESH), tr(STR_POEM_HISTORY), tr(STR_POEM_HISTORY));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
