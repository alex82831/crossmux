#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>

// SD-backed storage for the RSS app: the user-editable subscription list at
// /rss-feeds.json (defaults written on first run) and one fetch-cache file
// per feed under /.crosspoint/rss/, so every list/article screen renders from
// SD and works offline.
//
// Cache file layout (text-framed, bodies length-prefixed so no escaping):
//   CRSS1 <epoch>\n
//   repeated records:
//     @\n
//     <title, single line>\n
//     <date, single line>\n
//     <bodyLen decimal>\n
//     <bodyLen raw UTF-8 bytes>\n
namespace rssstore {

constexpr int kMaxFeeds = 16;
constexpr int kMaxArticles = 20;
constexpr size_t kMaxBodyBytes = 2048;
constexpr size_t kMaxTitleBytes = 160;
constexpr size_t kMaxDateBytes = 48;

struct FeedInfo {
  std::string name;
  std::string url;
};

// Loads /rss-feeds.json into out[0..maxFeeds); writes the default Chinese
// starter list first when the file is missing. Returns the feed count.
int loadFeeds(FeedInfo* out, int maxFeeds);

void cachePathFor(int feedIndex, char* out, size_t outLen);

// Titles/dates for the article list plus seek offsets for lazy body loads.
// ~4KB of strings for a full 20-article feed; lives as an activity member.
struct CacheIndex {
  int count = 0;
  uint32_t epoch = 0;
  std::string titles[kMaxArticles];
  std::string dates[kMaxArticles];
  uint32_t bodyOffset[kMaxArticles] = {};
  uint32_t bodyLen[kMaxArticles] = {};
};

bool readCacheIndex(const char* path, CacheIndex& out);
bool readCacheBody(const char* path, uint32_t offset, uint32_t len, std::string& out);

// Cheap cache probe for list subtitles: record count + fetch epoch only.
bool peekCache(const char* path, int& count, uint32_t& epoch);

// Streaming fetch of one feed into its cache file (Wi-Fi must already be up).
// Returns the number of cached articles; 0 means the fetch failed and any
// previous cache was left untouched.
int fetchFeedToCache(int feedIndex, const std::string& url);

// Streams records into <path>.tmp and renames over the cache on commit, so a
// failed fetch never destroys the previous cache.
class CacheWriter {
 public:
  bool open(int feedIndex);
  bool addItem(const char* title, const char* date, const std::string& body);
  bool commit();
  void abort();
  int count() const { return count_; }

 private:
  HalFile file_;
  char finalPath_[64] = {};
  char tmpPath_[72] = {};
  int count_ = 0;
};

}  // namespace rssstore
