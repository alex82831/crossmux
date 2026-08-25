#pragma once

#include <cstdint>

#include "KlotskiLayouts.h"
#include "activities/Activity.h"
#include "activities/apps/netkit/AppAbout.h"

// 华容道 (Klotski): classic 4x5 sliding-block puzzle, Three-Kingdoms themed.
// Confirm cycles the selected piece (or tap it), direction keys slide it one
// cell. Nine BFS-verified layouts, undo, per-layout best-step records, and
// mid-game resume across app exits. CN-build only (ENABLE_CHINESE_VERSION).
class KlotskiActivity final : public Activity {
 public:
  KlotskiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Klotski", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kMaxUndo = 512;

  struct PiecePos {
    int8_t x;
    int8_t y;
  };

  void startLayout(int index, bool resetSteps = true);
  bool tryMove(int piece, int dx, int dy);
  void undo();
  void handleBoardInput();
  void handleMenuInput();
  void handleWinInput();
  bool isWon() const;
  void boardGeometry(Rect& board, int& cell) const;
  const char* pieceLabel(int piece) const;
  void loadRecords();
  void saveRecords() const;

  int layout_ = 0;
  PiecePos pos_[klotski::kMaxPieces] = {};
  int selected_ = 0;
  uint16_t steps_ = 0;
  uint16_t best_[klotski::kLayoutCount] = {};  // 0 = no record yet
  // Undo log: piece index and direction packed per move; 512 entries bound
  // the memory at 1KB inside the heap-allocated activity.
  uint16_t undoCount_ = 0;
  uint8_t undoLog_[kMaxUndo] = {};
  bool menuOpen_ = false;
  uint8_t menuSelected_ = 0;
  bool won_ = false;
  bool aboutOpen_ = false;
};
