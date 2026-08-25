#pragma once

#include <string>

#include "RssStore.h"
#include "activities/UiListActivity.h"

// RSS entry screen: the subscription list from /rss-feeds.json with each
// feed's cache status as its subtitle. Confirm opens a feed's article list,
// Left/Right fetches every feed in one pass, Back returns to the Apps menu.
// CN-build only (ENABLE_CHINESE_VERSION).
class RssFeedListActivity final : public UiListActivity {
 public:
  RssFeedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("RssFeeds", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return feedCount_ > 0 ? feedCount_ : 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void drawFooter() override;

  void rebuildRows();
  void refreshAll();
  void doFetchAll();

  rssstore::FeedInfo feeds_[rssstore::kMaxFeeds];
  std::string subtitles_[rssstore::kMaxFeeds];
  freeink::ui::ListItem rowItems[rssstore::kMaxFeeds]{};
  int feedCount_ = 0;
};
