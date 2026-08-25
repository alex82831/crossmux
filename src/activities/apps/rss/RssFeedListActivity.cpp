#include "RssFeedListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "RssArticleListActivity.h"
#include "activities/ActivityManager.h"
#include "activities/apps/netkit/NetKit.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr uint32_t kMinValidEpoch = 1600000000;  // RTC unset guard
}

void RssFeedListActivity::onEnter() {
  UiListActivity::onEnter();
  feedCount_ = rssstore::loadFeeds(feeds_, rssstore::kMaxFeeds);
  rebuildRows();
}

void RssFeedListActivity::onExit() {
  // Fetch-all may have brought Wi-Fi up from this screen.
  netkit::teardownWifi();
  UiListActivity::onExit();
}

void RssFeedListActivity::rebuildRows() {
  if (feedCount_ == 0) {
    fui::ListItem hint;
    hint.label = tr(STR_RSS_NO_FEEDS);
    hint.enabled = false;
    rowItems[0] = hint;
    return;
  }
  for (int i = 0; i < feedCount_; ++i) {
    int cached = 0;
    uint32_t epoch = 0;
    char path[64];
    rssstore::cachePathFor(i, path, sizeof(path));
    if (rssstore::peekCache(path, cached, epoch)) {
      char buf[64];
      if (epoch > kMinValidEpoch) {
        time_t t = static_cast<time_t>(epoch);
        struct tm tmLocal;
        localtime_r(&t, &tmLocal);
        snprintf(buf, sizeof(buf), "%s %d · %02d-%02d %02d:%02d", tr(STR_RSS_CACHED_COUNT), cached, tmLocal.tm_mon + 1,
                 tmLocal.tm_mday, tmLocal.tm_hour, tmLocal.tm_min);
      } else {
        snprintf(buf, sizeof(buf), "%s %d", tr(STR_RSS_CACHED_COUNT), cached);
      }
      subtitles_[i] = buf;
    } else {
      subtitles_[i] = feeds_[i].url;
    }
    fui::ListItem item;
    item.label = feeds_[i].name.c_str();
    item.subtitle = subtitles_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

const char* RssFeedListActivity::headerTitle() const { return tr(STR_RSS_TITLE); }

void RssFeedListActivity::onBackButton() { activityManager.goToApps(); }

bool RssFeedListActivity::handleCustomInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (feedCount_ > 0) refreshAll();
    return true;
  }
  return false;
}

void RssFeedListActivity::refreshAll() {
  if (netkit::wifiConnected()) {
    doFetchAll();
    return;
  }
  startActivityForResultWith<WifiSelectionActivity>(
      [this](const ActivityResult& result) {
        if (!result.isCancelled && netkit::wifiConnected()) doFetchAll();
      },
      true);
}

void RssFeedListActivity::doFetchAll() {
  for (int i = 0; i < feedCount_; ++i) {
    char progress[96];
    snprintf(progress, sizeof(progress), "%s %d/%d\n%s", tr(STR_LOADING), i + 1, feedCount_, feeds_[i].name.c_str());
    renderer.clearScreen();
    UITheme::drawCenteredWrappedText(renderer, Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                                     UI_12_FONT_ID, progress, 3);
    renderer.displayBuffer();
    rssstore::fetchFeedToCache(i, feeds_[i].url);
  }
  rebuildRows();
  requestUpdate();
}

void RssFeedListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_RSS_FETCH_ALL), tr(STR_RSS_FETCH_ALL));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void RssFeedListActivity::activateIndex(const int index) {
  if (feedCount_ == 0 || index < 0 || index >= feedCount_) return;
  startActivityForResultWith<RssArticleListActivity>([this](const ActivityResult&) { rebuildRows(); }, index,
                                                     feeds_[index].name, feeds_[index].url);
}

void RssFeedListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, true);
  screen.list(props);
}
