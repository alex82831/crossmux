#include "QuoteFace.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "StandbyTime.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr const char* kLinesPath = "/standby/lines.txt";
// A quiet page deserves a slow rotation: often enough that closing the cover
// twice in an evening shows something new, rare enough that it is not churning
// the panel while the device sits on a desk.
constexpr uint32_t kRotateMinutes = 30;

bool isBlank(const char* s) {
  for (; *s; ++s) {
    if (*s != ' ' && *s != '\t' && *s != '\r') return false;
  }
  return true;
}

// Stream the file, counting non-blank lines and copying the one at `want`.
// Returns the number of lines seen. Nothing but the chosen line is retained,
// so the list may be far larger than RAM.
uint32_t scanForLine(const uint32_t want, char* out, const size_t cap) {
  out[0] = '\0';
  HalFile file;
  if (!Storage.openFileForRead("STANDBY", kLinesPath, file)) return 0;

  // ~640 bytes of stack, on the 8KB loop task, twice an hour. Kept local
  // rather than shared statics so nothing is left mutable between calls.
  char chunk[128];
  char current[512];
  size_t currentLen = 0;
  uint32_t index = 0;
  bool captured = false;

  const auto endLine = [&]() {
    current[currentLen] = '\0';
    // A UTF-8 BOM on the first line would otherwise render as a blank glyph.
    const char* text = current;
    if (index == 0 && currentLen >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
      text += 3;
    }
    currentLen = 0;
    if (isBlank(text)) return;  // blank separators do not count as entries
    if (index == want && !captured) {
      snprintf(out, cap, "%s", text);
      captured = true;
    }
    ++index;
  };

  for (;;) {
    const int n = file.read(chunk, sizeof(chunk));
    if (n <= 0) break;
    for (int i = 0; i < n; ++i) {
      const char c = chunk[i];
      if (c == '\n') {
        endLine();
      } else if (currentLen + 1 < sizeof(current)) {
        current[currentLen++] = c;
      }
      // An over-long line is clipped rather than split into two entries.
    }
  }
  if (currentLen > 0) endLine();  // last line without a trailing newline
  return index;
}

}  // namespace

void QuoteFace::onEnter() {
  lastRotateMinute_ = standby_time::getMinuteTick(0);
  load();
}

void QuoteFace::load() {
  lineCount_ = scanForLine(index_, line_, sizeof(line_));
  if (lineCount_ == 0) {
    line_[0] = '\0';
    index_ = 0;
  } else if (line_[0] == '\0') {
    // index_ ran past the end of a shorter file — wrap and take that one.
    index_ %= lineCount_;
    scanForLine(index_, line_, sizeof(line_));
  }
  dirty_ = true;
}

bool QuoteFace::tick() {
  const uint32_t minute = standby_time::getMinuteTick(0);
  if (minute - lastRotateMinute_ >= kRotateMinutes) {
    lastRotateMinute_ = minute;
    if (lineCount_ > 0) index_ = (index_ + 1) % lineCount_;
    load();
  }
  if (!dirty_) return false;
  dirty_ = false;
  return true;
}

void QuoteFace::onPageNext() {
  if (lineCount_ == 0) return;
  index_ = (index_ + 1) % lineCount_;
  load();
}

void QuoteFace::onPagePrev() {
  if (lineCount_ == 0) return;
  index_ = (index_ + lineCount_ - 1) % lineCount_;
  load();
}

void QuoteFace::onShake(const uint32_t seed) {
  if (lineCount_ == 0) return;
  index_ = seed % lineCount_;
  load();
}

StrId QuoteFace::titleId() const { return StrId::STR_STANDBY_FACE_LINES; }

uint32_t QuoteFace::secondsUntilNextWake() const {
  // Nothing changes between rotations, so sleep through to the next one.
  return kRotateMinutes * 60;
}

void QuoteFace::render(GfxRenderer& renderer, const Rect& viewport) {
  const int pad = 32;
  const Rect box{viewport.x + pad, viewport.y + viewport.height / 5, viewport.width - 2 * pad,
                 viewport.height * 3 / 5};

  if (lineCount_ == 0) {
    UITheme::drawCenteredWrappedText(renderer, box, UI_12_FONT_ID, tr(STR_STANDBY_LINES_EMPTY), 4);
    renderer.drawCenteredText(SMALL_FONT_ID, viewport.y + viewport.height - 60, kLinesPath);
    return;
  }

  // The line itself is the page. Larger type than a list would use, wrapped
  // across up to six lines, because this is meant to be read from across a
  // desk rather than in the hand.
  UITheme::drawCenteredWrappedText(renderer, box, NOTOSANS_16_FONT_ID, line_, 6);

  char counter[32];
  snprintf(counter, sizeof(counter), "%u / %u", static_cast<unsigned>(index_ + 1),
           static_cast<unsigned>(lineCount_));
  renderer.drawCenteredText(SMALL_FONT_ID, viewport.y + viewport.height - 46, counter);
}
