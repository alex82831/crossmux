#include "RssArticleViewActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "RssStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void RssArticleViewActivity::onEnter() {
  Activity::onEnter();

  std::string body;
  if (!rssstore::readCacheBody(cachePath_.c_str(), bodyOffset_, bodyLen_, body) || body.empty()) {
    body = tr(STR_RSS_NO_BODY);
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentW = safe.width - 2 * metrics.contentSidePadding;

  // Wrap once here; ≤2KB of text yields well under a hundred line strings.
  lines_ = renderer.wrappedText(UI_12_FONT_ID, body.c_str(), contentW, 400);

  const int contentTop = safe.y + metrics.topPadding;
  const int contentH = safe.y + safe.height - contentTop;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int headerBlockH = lineH + renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;
  const int pageIndicatorH = renderer.getLineHeight(SMALL_FONT_ID);
  linesPerPage_ = (contentH - headerBlockH - pageIndicatorH) / lineH;
  if (linesPerPage_ < 1) linesPerPage_ = 1;
  page_ = 0;
  requestUpdate();
}

int RssArticleViewActivity::pageCount() const {
  const int n = static_cast<int>(lines_.size());
  return (n + linesPerPage_ - 1) / linesPerPage_ > 0 ? (n + linesPerPage_ - 1) / linesPerPage_ : 1;
}

void RssArticleViewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                    mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                    mappedInput.wasReleased(MappedInputManager::Button::PageForward);
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                    mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                    mappedInput.wasReleased(MappedInputManager::Button::PageBack);
  if (next && page_ + 1 < pageCount()) {
    ++page_;
    requestUpdate();
  } else if (prev && page_ > 0) {
    --page_;
    requestUpdate();
  }
}

void RssArticleViewActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentX = safe.x + metrics.contentSidePadding;
  const int contentW = safe.width - 2 * metrics.contentSidePadding;

  renderer.clearScreen();

  int y = safe.y + metrics.topPadding;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);

  // Fixed article header on every page: one-line title + date.
  const std::string titleLine = renderer.truncatedText(UI_12_FONT_ID, title_.c_str(), contentW, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, contentX, y, titleLine.c_str(), true, EpdFontFamily::BOLD);
  y += lineH;
  if (!date_.empty()) {
    renderer.drawText(SMALL_FONT_ID, contentX, y, date_.c_str());
  }
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;

  const int firstLine = page_ * linesPerPage_;
  for (int i = 0; i < linesPerPage_ && firstLine + i < static_cast<int>(lines_.size()); ++i) {
    renderer.drawText(UI_12_FONT_ID, contentX, y, lines_[firstLine + i].c_str());
    y += lineH;
  }

  char pageText[24];
  snprintf(pageText, sizeof(pageText), "%d / %d", page_ + 1, pageCount());
  UITheme::drawCenteredText(renderer, Rect{0, 0, sw, renderer.getScreenHeight()}, SMALL_FONT_ID,
                            safe.y + safe.height - renderer.getLineHeight(SMALL_FONT_ID), pageText);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_APP_PREV_PAGE), tr(STR_APP_NEXT_PAGE));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
