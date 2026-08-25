#pragma once

#include <cstdint>

// Classic 4x5 Klotski (华容道) layouts. Every layout here was BFS-verified
// solvable; minSteps counts single-cell slides (the metric the app displays).
// Piece labels: the 2x2 block is 曹操, horizontals are 关羽/张辽/徐晃 and
// verticals 张飞/赵云/马超/黄忠 in order of appearance; 1x1 blocks are 兵.
namespace klotski {

constexpr int kBoardW = 4;
constexpr int kBoardH = 5;
constexpr int kMaxPieces = 10;
constexpr int kExitX = 1;  // Cao Cao must reach (1,3): flush with the exit.
constexpr int kExitY = 3;

enum class PieceType : uint8_t { Cao, Vert, Horz, Soldier };

struct PieceDef {
  PieceType type;
  int8_t x;
  int8_t y;
};

struct Layout {
  const char* name;
  uint16_t minSteps;
  PieceDef pieces[kMaxPieces];
};

// Ordered easy → hard by verified minimum slide count.
constexpr Layout kLayouts[] = {
    {"将拥曹营", 49, {{PieceType::Soldier,0,0},{PieceType::Cao,1,0},{PieceType::Soldier,3,0},{PieceType::Soldier,0,1},
                     {PieceType::Soldier,3,1},{PieceType::Vert,0,2},{PieceType::Horz,1,2},{PieceType::Vert,3,2},
                     {PieceType::Vert,1,3},{PieceType::Vert,2,3}}},
    {"水泄不通", 57, {{PieceType::Horz,0,0},{PieceType::Cao,2,0},{PieceType::Horz,0,1},{PieceType::Horz,0,2},
                     {PieceType::Vert,2,2},{PieceType::Vert,3,2},{PieceType::Soldier,0,3},{PieceType::Soldier,1,3},
                     {PieceType::Soldier,0,4},{PieceType::Soldier,1,4}}},
    {"雨声淅沥", 78, {{PieceType::Cao,0,0},{PieceType::Vert,2,0},{PieceType::Vert,3,0},{PieceType::Soldier,2,2},
                     {PieceType::Soldier,3,2},{PieceType::Horz,0,2},{PieceType::Soldier,0,3},{PieceType::Soldier,3,3},
                     {PieceType::Horz,0,4},{PieceType::Horz,2,4}}},
    {"齐头并进", 85, {{PieceType::Vert,0,0},{PieceType::Cao,1,0},{PieceType::Vert,3,0},{PieceType::Soldier,0,2},
                     {PieceType::Soldier,1,2},{PieceType::Soldier,2,2},{PieceType::Soldier,3,2},{PieceType::Vert,0,3},
                     {PieceType::Horz,1,3},{PieceType::Vert,3,3}}},
    {"兵分三路", 92, {{PieceType::Soldier,0,0},{PieceType::Cao,1,0},{PieceType::Soldier,3,0},{PieceType::Vert,0,1},
                     {PieceType::Horz,1,2},{PieceType::Vert,3,1},{PieceType::Vert,0,3},{PieceType::Soldier,1,3},
                     {PieceType::Soldier,2,3},{PieceType::Vert,3,3}}},
    {"指挥若定", 99, {{PieceType::Vert,0,0},{PieceType::Cao,1,0},{PieceType::Vert,3,0},{PieceType::Vert,0,2},
                     {PieceType::Soldier,1,2},{PieceType::Soldier,2,2},{PieceType::Vert,3,2},{PieceType::Horz,1,3},
                     {PieceType::Soldier,0,4},{PieceType::Soldier,3,4}}},
    {"过五关",   99, {{PieceType::Vert,0,0},{PieceType::Cao,1,0},{PieceType::Vert,3,0},{PieceType::Soldier,1,2},
                     {PieceType::Soldier,2,2},{PieceType::Horz,1,3},{PieceType::Vert,0,2},{PieceType::Vert,3,2},
                     {PieceType::Soldier,0,4},{PieceType::Soldier,3,4}}},
    {"小燕出巢", 114, {{PieceType::Cao,1,0},{PieceType::Vert,0,0},{PieceType::Vert,3,0},{PieceType::Horz,1,2},
                     {PieceType::Vert,0,2},{PieceType::Vert,3,2},{PieceType::Soldier,1,3},{PieceType::Soldier,2,3},
                     {PieceType::Soldier,1,4},{PieceType::Soldier,2,4}}},
    {"横刀立马", 116, {{PieceType::Vert,0,0},{PieceType::Cao,1,0},{PieceType::Vert,3,0},{PieceType::Vert,0,2},
                     {PieceType::Horz,1,2},{PieceType::Vert,3,2},{PieceType::Soldier,1,3},{PieceType::Soldier,2,3},
                     {PieceType::Soldier,0,4},{PieceType::Soldier,3,4}}},
};

constexpr int kLayoutCount = static_cast<int>(sizeof(kLayouts) / sizeof(kLayouts[0]));

inline void pieceSize(const PieceType type, int& w, int& h) {
  switch (type) {
    case PieceType::Cao:
      w = 2;
      h = 2;
      return;
    case PieceType::Vert:
      w = 1;
      h = 2;
      return;
    case PieceType::Horz:
      w = 2;
      h = 1;
      return;
    case PieceType::Soldier:
    default:
      w = 1;
      h = 1;
      return;
  }
}

}  // namespace klotski
