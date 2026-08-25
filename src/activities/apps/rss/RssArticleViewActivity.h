#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

// Full-screen paged view of one cached article body. The ≤2KB body is loaded
// lazily from the feed cache by offset, wrapped once on entry, and paged with
// the vertical/page keys.
class RssArticleViewActivity final : public Activity {
 public:
  RssArticleViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string cachePath,
                         uint32_t bodyOffset, uint32_t bodyLen, std::string title, std::string date)
      : Activity("RssArticle", renderer, mappedInput),
        cachePath_(std::move(cachePath)),
        bodyOffset_(bodyOffset),
        bodyLen_(bodyLen),
        title_(std::move(title)),
        date_(std::move(date)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int pageCount() const;

  std::string cachePath_;
  uint32_t bodyOffset_;
  uint32_t bodyLen_;
  std::string title_;
  std::string date_;
  // Wrapped once in onEnter (≤2KB body → well under a hundred short lines);
  // rendering then only slices this vector per page.
  std::vector<std::string> lines_;
  int linesPerPage_ = 1;
  int page_ = 0;
};
