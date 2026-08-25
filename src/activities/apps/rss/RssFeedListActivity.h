#pragma once

#include "RssStore.h"
#include "activities/UiListActivity.h"

// RSS entry screen: the subscription list from /rss-feeds.json. Selecting a
// feed opens its article list; Back returns to the Apps menu. CN-build only
// (ENABLE_CHINESE_VERSION).
class RssFeedListActivity final : public UiListActivity {
 public:
  RssFeedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("RssFeeds", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override { return feedCount_ > 0 ? feedCount_ : 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;

  rssstore::FeedInfo feeds_[rssstore::kMaxFeeds];
  freeink::ui::ListItem rowItems[rssstore::kMaxFeeds]{};
  int feedCount_ = 0;
};
