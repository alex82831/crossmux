#pragma once

#include <string>

#include "RssStore.h"
#include "activities/UiListActivity.h"

// Article list for one feed: renders from the SD fetch-cache (so it works
// offline) and refreshes on demand over Wi-Fi with a streaming parse — feed
// bodies never sit in RAM. Confirm opens an article, Left/Right refreshes.
class RssArticleListActivity final : public UiListActivity {
 public:
  RssArticleListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int feedIndex, std::string feedName,
                         std::string feedUrl)
      : UiListActivity("RssArticles", renderer, mappedInput),
        feedIndex_(feedIndex),
        feedName_(std::move(feedName)),
        feedUrl_(std::move(feedUrl)) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return index_.count > 0 ? index_.count : 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return feedName_.c_str(); }
  bool handleCustomInput() override;
  void drawFooter() override;

  void loadCache();
  void refresh();
  void doFetch();
  void drawBusy(const char* message);

  int feedIndex_;
  std::string feedName_;
  std::string feedUrl_;
  char cachePath_[64] = {};
  // ~4KB of titles/dates for a full feed; freed with the activity.
  rssstore::CacheIndex index_;
  freeink::ui::ListItem rowItems[rssstore::kMaxArticles]{};
  bool fetchFailed_ = false;
};
