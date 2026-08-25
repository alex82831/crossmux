#include "RssFeedListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "RssArticleListActivity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void RssFeedListActivity::onEnter() {
  UiListActivity::onEnter();
  feedCount_ = rssstore::loadFeeds(feeds_, rssstore::kMaxFeeds);

  if (feedCount_ == 0) {
    fui::ListItem hint;
    hint.label = tr(STR_RSS_NO_FEEDS);
    hint.enabled = false;
    rowItems[0] = hint;
    return;
  }
  for (int i = 0; i < feedCount_; ++i) {
    fui::ListItem item;
    item.label = feeds_[i].name.c_str();
    item.subtitle = feeds_[i].url.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

const char* RssFeedListActivity::headerTitle() const { return tr(STR_RSS_TITLE); }

void RssFeedListActivity::onBackButton() { activityManager.goToApps(); }

void RssFeedListActivity::activateIndex(const int index) {
  if (feedCount_ == 0 || index < 0 || index >= feedCount_) return;
  startActivityForResultWith<RssArticleListActivity>([](const ActivityResult&) {}, index, feeds_[index].name,
                                                     feeds_[index].url);
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
