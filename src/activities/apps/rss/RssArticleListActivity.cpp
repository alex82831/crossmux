#include "RssArticleListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "RssArticleViewActivity.h"
#include "RssFeedParser.h"
#include "activities/apps/netkit/NetKit.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "network/HttpDownloader.h"

namespace fui = freeink::ui;

namespace {
// Transport-level guard: stop pulling a runaway feed once this much raw XML
// has been consumed; 20 items are normally seen well before this.
constexpr size_t kMaxFeedBytes = 256 * 1024;
}  // namespace

void RssArticleListActivity::onEnter() {
  UiListActivity::onEnter();
  rssstore::cachePathFor(feedIndex_, cachePath_, sizeof(cachePath_));
  loadCache();
}

void RssArticleListActivity::onExit() {
  // Wi-Fi may have been brought up for a refresh; this screen's flow owns it.
  netkit::teardownWifi();
  UiListActivity::onExit();
}

void RssArticleListActivity::loadCache() {
  rssstore::readCacheIndex(cachePath_, index_);
  if (index_.count == 0) {
    fui::ListItem hint;
    hint.label = fetchFailed_ ? tr(STR_APP_FETCH_FAILED) : tr(STR_RSS_EMPTY_PROMPT);
    hint.enabled = false;
    rowItems[0] = hint;
    return;
  }
  for (int i = 0; i < index_.count; ++i) {
    fui::ListItem item;
    item.label = index_.titles[i].c_str();
    item.subtitle = index_.dates[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
  if (nav.selected >= index_.count) nav.selected = 0;
}

bool RssArticleListActivity::handleCustomInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    refresh();
    return true;
  }
  return false;
}

void RssArticleListActivity::refresh() {
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

void RssArticleListActivity::drawBusy(const char* message) {
  renderer.clearScreen();
  UITheme::drawCenteredWrappedText(renderer, Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                                   UI_12_FONT_ID, message, 2);
  renderer.displayBuffer();
}

void RssArticleListActivity::doFetch() {
  drawBusy(tr(STR_LOADING));

  rssstore::CacheWriter writer;
  if (!writer.open(feedIndex_)) {
    LOG_ERR("RSS", "cache open failed for feed %d", feedIndex_);
    fetchFailed_ = true;
    loadCache();
    requestUpdate();
    return;
  }

  RssFeedParser parser([&writer](const RssFeedParser::Item& item) {
    if (!writer.addItem(item.title, item.date, item.body)) return false;
    return writer.count() < rssstore::kMaxArticles;
  });

  size_t consumed = 0;
  HttpDownloader::fetchUrl(feedUrl_, [&](const uint8_t* data, const size_t len) {
    consumed += len;
    if (consumed > kMaxFeedBytes) return false;
    return parser.write(data, len);
  });

  // The transfer is aborted on purpose once the article cap is hit, so judge
  // by what was parsed, not by the transport result.
  if (writer.count() > 0) {
    fetchFailed_ = !writer.commit();
  } else {
    writer.abort();
    fetchFailed_ = true;
    LOG_ERR("RSS", "no articles parsed from feed %d (%u bytes)", feedIndex_, static_cast<unsigned>(consumed));
  }
  loadCache();
  requestUpdate();
}

void RssArticleListActivity::activateIndex(const int index) {
  if (index_.count == 0 || index < 0 || index >= index_.count) return;
  startActivityForResultWith<RssArticleViewActivity>([](const ActivityResult&) {}, std::string(cachePath_),
                                                     index_.bodyOffset[index], index_.bodyLen[index],
                                                     index_.titles[index], index_.dates[index]);
}

void RssArticleListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_APP_REFRESH), tr(STR_APP_REFRESH));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void RssArticleListActivity::buildScreen(UiScreen& screen) {
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
