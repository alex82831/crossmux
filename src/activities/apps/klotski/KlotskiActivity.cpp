#include "KlotskiActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/apps/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

using klotski::kBoardH;
using klotski::kBoardW;
using klotski::kLayoutCount;
using klotski::kLayouts;
using klotski::kMaxPieces;
using klotski::PieceType;

namespace {
constexpr const char* kSavePath = "/.crosspoint/klotski.bin";
constexpr uint32_t kSaveMagic = 0x315A4C4B;  // "KLZ1"

// Save record: magic + bests + resumable in-progress game.
struct SaveRecord {
  uint32_t magic = kSaveMagic;
  uint16_t best[kLayoutCount] = {};
  uint8_t layout = 0;
  uint8_t selected = 0;
  uint16_t steps = 0;
  int8_t px[kMaxPieces] = {};
  int8_t py[kMaxPieces] = {};
  uint8_t inProgress = 0;
};

const char* kVertNames[4] = {"张飞", "赵云", "马超", "黄忠"};
const char* kHorzNames[3] = {"关羽", "张辽", "徐晃"};

constexpr int kDirDx[4] = {0, 0, -1, 1};
constexpr int kDirDy[4] = {-1, 1, 0, 0};
}  // namespace

void KlotskiActivity::onEnter() {
  Activity::onEnter();
  loadRecords();
  requestUpdate();
}

void KlotskiActivity::onExit() {
  saveRecords();
  Activity::onExit();
}

void KlotskiActivity::startLayout(const int index, const bool resetSteps) {
  layout_ = ((index % kLayoutCount) + kLayoutCount) % kLayoutCount;
  const klotski::Layout& l = kLayouts[layout_];
  for (int i = 0; i < kMaxPieces; ++i) {
    pos_[i].x = l.pieces[i].x;
    pos_[i].y = l.pieces[i].y;
  }
  if (resetSteps) {
    steps_ = 0;
    undoCount_ = 0;
  }
  selected_ = 0;
  won_ = false;
  menuOpen_ = false;
}

const char* KlotskiActivity::pieceLabel(const int piece) const {
  const klotski::Layout& l = kLayouts[layout_];
  int vert = 0;
  int horz = 0;
  for (int i = 0; i < piece; ++i) {
    if (l.pieces[i].type == PieceType::Vert) ++vert;
    if (l.pieces[i].type == PieceType::Horz) ++horz;
  }
  switch (l.pieces[piece].type) {
    case PieceType::Cao:
      return "曹操";
    case PieceType::Vert:
      return kVertNames[vert % 4];
    case PieceType::Horz:
      return kHorzNames[horz % 3];
    case PieceType::Soldier:
    default:
      return "兵";
  }
}

bool KlotskiActivity::isWon() const {
  const klotski::Layout& l = kLayouts[layout_];
  for (int i = 0; i < kMaxPieces; ++i) {
    if (l.pieces[i].type == PieceType::Cao) {
      return pos_[i].x == klotski::kExitX && pos_[i].y == klotski::kExitY;
    }
  }
  return false;
}

bool KlotskiActivity::tryMove(const int piece, const int dx, const int dy) {
  const klotski::Layout& l = kLayouts[layout_];
  int w = 0, h = 0;
  klotski::pieceSize(l.pieces[piece].type, w, h);
  const int nx = pos_[piece].x + dx;
  const int ny = pos_[piece].y + dy;
  if (nx < 0 || ny < 0 || nx + w > kBoardW || ny + h > kBoardH) return false;
  for (int other = 0; other < kMaxPieces; ++other) {
    if (other == piece) continue;
    int ow = 0, oh = 0;
    klotski::pieceSize(l.pieces[other].type, ow, oh);
    const bool overlap = nx < pos_[other].x + ow && pos_[other].x < nx + w && ny < pos_[other].y + oh &&
                         pos_[other].y < ny + h;
    if (overlap) return false;
  }
  pos_[piece].x = static_cast<int8_t>(nx);
  pos_[piece].y = static_cast<int8_t>(ny);
  return true;
}

void KlotskiActivity::undo() {
  if (undoCount_ == 0) return;
  const uint8_t entry = undoLog_[--undoCount_];
  const int piece = entry >> 2;
  const int dir = entry & 3;
  // Reverse the recorded slide; geometry is always valid going backwards.
  pos_[piece].x = static_cast<int8_t>(pos_[piece].x - kDirDx[dir]);
  pos_[piece].y = static_cast<int8_t>(pos_[piece].y - kDirDy[dir]);
  if (steps_ > 0) --steps_;
  won_ = false;
}

void KlotskiActivity::loop() {
  if (menuOpen_) {
    handleMenuInput();
    return;
  }
  if (won_) {
    handleWinInput();
    return;
  }
  handleBoardInput();
}

void KlotskiActivity::handleBoardInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    menuOpen_ = true;
    menuSelected_ = 0;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selected_ = (selected_ + 1) % kMaxPieces;
    requestUpdate();
    return;
  }

  int dir = -1;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) dir = 0;
  else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) dir = 1;
  else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) dir = 2;
  else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) dir = 3;
  if (dir >= 0) {
    if (tryMove(selected_, kDirDx[dir], kDirDy[dir])) {
      if (undoCount_ < kMaxUndo) undoLog_[undoCount_++] = static_cast<uint8_t>((selected_ << 2) | dir);
      if (steps_ < 0xFFFF) ++steps_;
      if (isWon()) {
        won_ = true;
        if (best_[layout_] == 0 || steps_ < best_[layout_]) {
          best_[layout_] = steps_;
          saveRecords();
        }
      }
      requestUpdate();
    }
    return;
  }

  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    Rect board{};
    int cell = 0;
    boardGeometry(board, cell);
    const klotski::Layout& l = kLayouts[layout_];
    for (int i = 0; i < kMaxPieces; ++i) {
      int w = 0, h = 0;
      klotski::pieceSize(l.pieces[i].type, w, h);
      const Rect r{board.x + pos_[i].x * cell, board.y + pos_[i].y * cell, w * cell, h * cell};
      if (tx >= r.x && tx < r.x + r.width && ty >= r.y && ty < r.y + r.height) {
        selected_ = i;
        requestUpdate();
        return;
      }
    }
  }
}

void KlotskiActivity::handleMenuInput() {
  static constexpr int kMenuCount = 6;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect panel = gameMenuPanelRect(renderer.getScreenWidth(), renderer.getScreenHeight(), 300,
                                       metrics.menuRowHeight, metrics.menuRowHeight, kMenuCount);
  const auto result = gameHandleMenuInput(mappedInput, panel, metrics.menuRowHeight, metrics.menuRowHeight,
                                          kMenuCount, menuSelected_);
  if (result == GameMenuInputResult::SelectionChanged) {
    requestUpdate();
    return;
  }
  if (result == GameMenuInputResult::Dismissed) {
    menuOpen_ = false;
    requestUpdate();
    return;
  }
  if (result != GameMenuInputResult::Activated) return;
  switch (menuSelected_) {
    case 0:  // continue
      menuOpen_ = false;
      break;
    case 1:  // undo one
      menuOpen_ = false;
      undo();
      break;
    case 2:  // restart
      startLayout(layout_);
      break;
    case 3:  // previous layout
      startLayout(layout_ - 1);
      break;
    case 4:  // next layout
      startLayout(layout_ + 1);
      break;
    case 5:  // exit
      activityManager.goToApps();
      return;
    default:
      break;
  }
  requestUpdate();
}

void KlotskiActivity::handleWinInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    startLayout(layout_ + 1);
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
  }
}

void KlotskiActivity::boardGeometry(Rect& board, int& cell) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int top = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int statusH = renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const int availW = safe.width - 2 * metrics.contentSidePadding;
  const int availH = safe.y + safe.height - top - statusH;
  cell = availW / kBoardW < availH / kBoardH ? availW / kBoardW : availH / kBoardH;
  board.width = cell * kBoardW;
  board.height = cell * kBoardH;
  board.x = safe.x + (safe.width - board.width) / 2;
  board.y = top;
}

void KlotskiActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();

  renderer.clearScreen();
  char subtitle[48];
  snprintf(subtitle, sizeof(subtitle), "%d/%d", layout_ + 1, kLayoutCount);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, kLayouts[layout_].name, subtitle);

  Rect board{};
  int cell = 0;
  boardGeometry(board, cell);

  // Board frame with the exit gap under Cao Cao's target columns.
  renderer.drawRect(board.x - 2, board.y - 2, board.width + 4, board.height + 4, true);
  const int exitX0 = board.x + klotski::kExitX * cell;
  renderer.fillRect(exitX0, board.y + board.height, 2 * cell, 3, false);

  const klotski::Layout& l = kLayouts[layout_];
  for (int i = 0; i < kMaxPieces; ++i) {
    int w = 0, h = 0;
    klotski::pieceSize(l.pieces[i].type, w, h);
    const Rect r{board.x + pos_[i].x * cell + 2, board.y + pos_[i].y * cell + 2, w * cell - 4, h * cell - 4};
    renderer.drawRect(r.x, r.y, r.width, r.height, true);
    if (i == selected_) {
      renderer.drawRect(r.x + 2, r.y + 2, r.width - 4, r.height - 4, true);
      renderer.drawRect(r.x + 3, r.y + 3, r.width - 6, r.height - 6, true);
    }
    const char* label = pieceLabel(i);
    const auto style = l.pieces[i].type == PieceType::Cao ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    if (l.pieces[i].type == PieceType::Vert) {
      // Stack the two characters of the general's name vertically.
      char ch[8] = {};
      const int lineH = renderer.getTextHeight(UI_12_FONT_ID);
      const int totalH = lineH * 2;
      int cy = r.y + gameCenterY(r.height, totalH);
      const char* p = label;
      for (int part = 0; part < 2 && *p != '\0'; ++part) {
        int n = 1;
        while ((p[n] & 0xC0) == 0x80) ++n;  // one UTF-8 char
        memcpy(ch, p, n);
        ch[n] = '\0';
        renderer.drawText(UI_12_FONT_ID, r.x + (r.width - renderer.getTextWidth(UI_12_FONT_ID, ch)) / 2, cy, ch);
        cy += lineH;
        p += n;
      }
    } else {
      const int fontId = l.pieces[i].type == PieceType::Soldier ? UI_10_FONT_ID : UI_12_FONT_ID;
      const int tw = renderer.getTextWidth(fontId, label, style);
      const int th = renderer.getTextHeight(fontId);
      renderer.drawText(fontId, r.x + (r.width - tw) / 2, r.y + gameCenterY(r.height, th), label, true, style);
    }
  }

  // Status line: steps, verified minimum, personal best.
  char status[96];
  if (best_[layout_] > 0) {
    snprintf(status, sizeof(status), "%s %u · %s %u · %s %u", tr(STR_KLOTSKI_STEPS), steps_, tr(STR_KLOTSKI_MIN),
             kLayouts[layout_].minSteps, tr(STR_KLOTSKI_BEST), best_[layout_]);
  } else {
    snprintf(status, sizeof(status), "%s %u · %s %u", tr(STR_KLOTSKI_STEPS), steps_, tr(STR_KLOTSKI_MIN),
             kLayouts[layout_].minSteps);
  }
  UITheme::drawCenteredText(renderer, Rect{0, 0, sw, renderer.getScreenHeight()}, UI_10_FONT_ID,
                            board.y + board.height + metrics.verticalSpacing, status);

  if (menuOpen_) {
    const GameMenuItem items[6] = {{tr(STR_KLOTSKI_CONTINUE), nullptr}, {tr(STR_KLOTSKI_UNDO), nullptr},
                                   {tr(STR_KLOTSKI_RESTART), nullptr},  {tr(STR_KLOTSKI_PREV), nullptr},
                                   {tr(STR_KLOTSKI_NEXT), nullptr},     {tr(STR_EXIT), nullptr}};
    const Rect panel = gameMenuPanelRect(sw, renderer.getScreenHeight(), 300, metrics.menuRowHeight,
                                         metrics.menuRowHeight, 6);
    gameDrawMenu(renderer, panel, metrics.menuRowHeight, metrics.menuRowHeight, tr(STR_KLOTSKI_TITLE), items, 6,
                 menuSelected_);
  } else if (won_) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s\n%s %u · %s %u", tr(STR_KLOTSKI_WIN), tr(STR_KLOTSKI_STEPS), steps_,
             tr(STR_KLOTSKI_BEST), best_[layout_]);
    const Rect panel = gameMenuPanelRect(sw, renderer.getScreenHeight(), 320, metrics.menuRowHeight * 2, 0, 0);
    renderer.fillRect(panel.x, panel.y, panel.width, metrics.menuRowHeight * 3, false);
    renderer.drawRect(panel.x, panel.y, panel.width, metrics.menuRowHeight * 3, true);
    UITheme::drawCenteredWrappedText(renderer,
                                     Rect{panel.x + 8, panel.y + 8, panel.width - 16, metrics.menuRowHeight * 3 - 16},
                                     UI_12_FONT_ID, msg, 3, true, EpdFontFamily::BOLD);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_KLOTSKI_MENU), won_ ? tr(STR_KLOTSKI_NEXT) : tr(STR_KLOTSKI_PICK), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void KlotskiActivity::loadRecords() {
  SaveRecord rec;
  HalFile file;
  bool resumed = false;
  if (Storage.openFileForRead("KLZ", kSavePath, file)) {
    if (file.read(&rec, sizeof(rec)) == static_cast<int>(sizeof(rec)) && rec.magic == kSaveMagic) {
      memcpy(best_, rec.best, sizeof(best_));
      if (rec.inProgress != 0 && rec.layout < kLayoutCount) {
        startLayout(rec.layout);
        for (int i = 0; i < kMaxPieces; ++i) {
          pos_[i].x = rec.px[i];
          pos_[i].y = rec.py[i];
        }
        steps_ = rec.steps;
        selected_ = rec.selected % kMaxPieces;
        won_ = isWon();
        resumed = true;
      }
    }
  }
  if (!resumed) startLayout(0);
}

void KlotskiActivity::saveRecords() const {
  SaveRecord rec;
  memcpy(rec.best, best_, sizeof(best_));
  rec.layout = static_cast<uint8_t>(layout_);
  rec.selected = static_cast<uint8_t>(selected_);
  rec.steps = steps_;
  for (int i = 0; i < kMaxPieces; ++i) {
    rec.px[i] = pos_[i].x;
    rec.py[i] = pos_[i].y;
  }
  rec.inProgress = (!won_ && steps_ > 0) ? 1 : 0;
  HalFile file;
  if (!Storage.openFileForWrite("KLZ", kSavePath, file)) {
    LOG_ERR("KLZ", "save open failed");
    return;
  }
  file.write(&rec, sizeof(rec));
}
