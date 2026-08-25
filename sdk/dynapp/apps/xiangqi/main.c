// 中国象棋 — Xiangqi vs an alpha-beta AI. Full move rules (车马炮相仕帅兵,
// river, palace, horse-hobbling, cannon screen, flying-general), check
// detection, 2-ply search with material + mobility eval. Human plays red
// (bottom), moves cursor with the D-pad, Confirm selects/moves. Ported to
// .eapp.

#include "app.h"

// Board: 9 files (x 0..8) × 10 ranks (y 0..9). y=0 top (black), y=9 bottom
// (red). Pieces: positive = red (human), negative = black (AI). 0 = empty.
// 1车 2马 3炮 4相 5仕 6帅 7兵
enum { R_CHE = 1, R_MA, R_PAO, R_XIANG, R_SHI, R_JIANG, R_BING };
static signed char g_b[10][9];
static int g_selX, g_selY;              // cursor
static int g_pickX = -1, g_pickY = -1;  // chosen piece (-1 none)
static int g_turn;                      // 1 red(human) -1 black(ai)
static int g_over;                      // 0 playing, 1 red win, 2 black win
static int g_view;
static AppAbout g_about;

static const signed char kInit[10][9] = {
    {-R_CHE, -R_MA, -R_XIANG, -R_SHI, -R_JIANG, -R_SHI, -R_XIANG, -R_MA, -R_CHE},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, -R_PAO, 0, 0, 0, 0, 0, -R_PAO, 0},
    {-R_BING, 0, -R_BING, 0, -R_BING, 0, -R_BING, 0, -R_BING},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {R_BING, 0, R_BING, 0, R_BING, 0, R_BING, 0, R_BING},
    {0, R_PAO, 0, 0, 0, 0, 0, R_PAO, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
    {R_CHE, R_MA, R_XIANG, R_SHI, R_JIANG, R_SHI, R_XIANG, R_MA, R_CHE},
};

static int on_board(int x, int y) { return x >= 0 && x < 9 && y >= 0 && y < 10; }
static int same_side(int a, int b) { return (a > 0 && b > 0) || (a < 0 && b < 0); }

// Is move (fx,fy)->(tx,ty) legal for the piece at (fx,fy)? (pseudo-legal,
// ignoring self-check; that is filtered separately).
static int pseudo_legal(int fx, int fy, int tx, int ty) {
  const int p = g_b[fy][fx];
  if (p == 0 || !on_board(tx, ty)) return 0;
  const int dst = g_b[ty][tx];
  if (same_side(p, dst)) return 0;
  const int type = p > 0 ? p : -p;
  const int dx = tx - fx, dy = ty - fy;
  const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  const int red = p > 0;

  if (type == R_CHE) {
    if (dx && dy) return 0;
    const int sx = dx ? (dx > 0 ? 1 : -1) : 0, sy = dy ? (dy > 0 ? 1 : -1) : 0;
    for (int x = fx + sx, y = fy + sy; x != tx || y != ty; x += sx, y += sy)
      if (g_b[y][x]) return 0;
    return 1;
  }
  if (type == R_MA) {
    if (!((adx == 1 && ady == 2) || (adx == 2 && ady == 1))) return 0;
    // hobbling leg
    if (adx == 2) {
      if (g_b[fy][fx + dx / 2]) return 0;
    } else {
      if (g_b[fy + dy / 2][fx]) return 0;
    }
    return 1;
  }
  if (type == R_PAO) {
    if (dx && dy) return 0;
    const int sx = dx ? (dx > 0 ? 1 : -1) : 0, sy = dy ? (dy > 0 ? 1 : -1) : 0;
    int screens = 0;
    for (int x = fx + sx, y = fy + sy; x != tx || y != ty; x += sx, y += sy)
      if (g_b[y][x]) ++screens;
    if (dst == 0) return screens == 0;  // move: clear path
    return screens == 1;                // capture: exactly one screen
  }
  if (type == R_XIANG) {
    if (adx != 2 || ady != 2) return 0;
    if (g_b[fy + dy / 2][fx + dx / 2]) return 0;  // eye blocked
    if (red && ty < 5) return 0;                  // cannot cross river
    if (!red && ty > 4) return 0;
    return 1;
  }
  if (type == R_SHI) {
    if (adx != 1 || ady != 1) return 0;
    if (tx < 3 || tx > 5) return 0;
    if (red && ty < 7) return 0;
    if (!red && ty > 2) return 0;
    return 1;
  }
  if (type == R_JIANG) {
    // Flying-general: general may "capture" opposing general down an open file.
    if (dst != 0 && (dst == R_JIANG || dst == -R_JIANG) && dx == 0) {
      const int sy = dy > 0 ? 1 : -1;
      for (int y = fy + sy; y != ty; y += sy)
        if (g_b[y][fx]) return 0;
      return 1;
    }
    if (adx + ady != 1) return 0;
    if (tx < 3 || tx > 5) return 0;
    if (red && ty < 7) return 0;
    if (!red && ty > 2) return 0;
    return 1;
  }
  if (type == R_BING) {
    if (red) {
      if (dy == -1 && dx == 0) return 1;              // forward
      if (fy <= 4 && ady == 0 && adx == 1) return 1;  // sideways past river
      return 0;
    } else {
      if (dy == 1 && dx == 0) return 1;
      if (fy >= 5 && ady == 0 && adx == 1) return 1;
      return 0;
    }
  }
  return 0;
}

static void find_general(int side, int* gx, int* gy) {
  const int target = side > 0 ? R_JIANG : -R_JIANG;
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 9; ++x)
      if (g_b[y][x] == target) {
        *gx = x;
        *gy = y;
        return;
      }
  *gx = -1;
  *gy = -1;
}

// Is `side` in check?
static int in_check(int side) {
  int gx, gy;
  find_general(side, &gx, &gy);
  if (gx < 0) return 1;  // general captured
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 9; ++x) {
      const int p = g_b[y][x];
      if (p == 0 || same_side(p, side > 0 ? 1 : -1)) continue;
      if (pseudo_legal(x, y, gx, gy)) return 1;
    }
  return 0;
}

// Full legality: pseudo-legal and does not leave own general in check.
static int legal(int fx, int fy, int tx, int ty) {
  if (!pseudo_legal(fx, fy, tx, ty)) return 0;
  const int p = g_b[fy][fx], cap = g_b[ty][tx];
  g_b[ty][tx] = p;
  g_b[fy][fx] = 0;
  const int side = p > 0 ? 1 : -1;
  const int bad = in_check(side);
  g_b[fy][fx] = p;
  g_b[ty][tx] = cap;
  return !bad;
}

// ---- AI: material + mobility eval, 2-ply alpha-beta --------------------
static const int kVal[8] = {0, 900, 400, 450, 200, 200, 10000, 100};
static int eval_board(void) {
  int score = 0;  // positive = good for red
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 9; ++x) {
      const int p = g_b[y][x];
      if (!p) continue;
      const int t = p > 0 ? p : -p;
      int v = kVal[t];
      if (t == R_BING) {  // advanced soldiers worth more
        if (p > 0 && y <= 4) v += 60;
        if (p < 0 && y >= 5) v += 60;
      }
      score += p > 0 ? v : -v;
    }
  return score;
}

typedef struct {
  signed char fx, fy, tx, ty;
} Move;

static int gen_moves(int side, Move* out, int cap) {
  int n = 0;
  for (int fy = 0; fy < 10 && n < cap; ++fy)
    for (int fx = 0; fx < 9 && n < cap; ++fx) {
      const int p = g_b[fy][fx];
      if (p == 0 || !same_side(p, side > 0 ? 1 : -1)) continue;
      for (int ty = 0; ty < 10 && n < cap; ++ty)
        for (int tx = 0; tx < 9 && n < cap; ++tx)
          if (legal(fx, fy, tx, ty)) {
            out[n].fx = fx;
            out[n].fy = fy;
            out[n].tx = tx;
            out[n].ty = ty;
            ++n;
          }
    }
  return n;
}

// Negamax from `side` perspective (side=-1 black). Returns score for red.
static int search(int side, int depth, int alpha, int beta) {
  if (depth == 0) return eval_board();
  Move mv[120];
  const int n = gen_moves(side, mv, 120);
  if (n == 0) return side > 0 ? -100000 : 100000;  // checkmate/stalemate ~ loss
  int best = side > 0 ? -1000000 : 1000000;
  for (int i = 0; i < n; ++i) {
    const int p = g_b[mv[i].fy][mv[i].fx], cap = g_b[mv[i].ty][mv[i].tx];
    g_b[mv[i].ty][mv[i].tx] = p;
    g_b[mv[i].fy][mv[i].fx] = 0;
    const int val = search(-side, depth - 1, alpha, beta);
    g_b[mv[i].fy][mv[i].fx] = p;
    g_b[mv[i].ty][mv[i].tx] = cap;
    if (side > 0) {
      if (val > best) best = val;
      if (best > alpha) alpha = best;
    } else {
      if (val < best) best = val;
      if (best < beta) beta = best;
    }
    if (beta <= alpha) break;
  }
  return best;
}

static void ai_move(const CpApi* api) {
  Move mv[120];
  const int n = gen_moves(-1, mv, 120);
  if (n == 0) {
    g_over = 1;
    return;
  }
  int bestVal = 1000000, bestI = 0;
  for (int i = 0; i < n; ++i) {
    const int p = g_b[mv[i].fy][mv[i].fx], cap = g_b[mv[i].ty][mv[i].tx];
    g_b[mv[i].ty][mv[i].tx] = p;
    g_b[mv[i].fy][mv[i].fx] = 0;
    int val = search(1, 1, -1000000, 1000000);  // 2-ply total
    val += (int)(api->random_u32() % 9) - 4;    // tie jitter
    g_b[mv[i].fy][mv[i].fx] = p;
    g_b[mv[i].ty][mv[i].tx] = cap;
    if (val < bestVal) {
      bestVal = val;
      bestI = i;
    }
  }
  const Move* m = &mv[bestI];
  g_b[m->ty][m->tx] = g_b[m->fy][m->fx];
  g_b[m->fy][m->fx] = 0;
  g_turn = 1;
  // Human has no legal reply → lost.
  Move tmp[120];
  if (gen_moves(1, tmp, 120) == 0) g_over = 2;
}

static void reset(void) {
  memcpy(g_b, kInit, sizeof(g_b));
  g_selX = 4;
  g_selY = 9;
  g_pickX = g_pickY = -1;
  g_turn = 1;
  g_over = 0;
}

static int32_t on_enter(const CpApi* api) {
  (void)api;
  g_view = 0;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (g_view == 0) {
    if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
    if (in->released & CP_BTN_CONFIRM) {
      reset();
      g_view = 1;
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }

  if (in->released & CP_BTN_BACK) {
    if (g_pickX >= 0) {
      g_pickX = g_pickY = -1;
      return CP_LOOP_RENDER;
    }
    g_view = 0;
    return CP_LOOP_RENDER;
  }
  if (g_over) {
    if (in->released & CP_BTN_CONFIRM) {
      reset();
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  if (g_turn == -1) {  // AI turn (blocking)
    ai_move(api);
    return CP_LOOP_RENDER;
  }
  uint32_t f = CP_LOOP_IDLE;
  if (in->released & CP_BTN_LEFT) {
    g_selX = (g_selX + 8) % 9;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_RIGHT) {
    g_selX = (g_selX + 1) % 9;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_UP) {
    g_selY = (g_selY + 9) % 10;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_DOWN) {
    g_selY = (g_selY + 1) % 10;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_CONFIRM) {
    if (g_pickX < 0) {
      if (g_b[g_selY][g_selX] > 0) {
        g_pickX = g_selX;
        g_pickY = g_selY;
      }
    } else {
      if (legal(g_pickX, g_pickY, g_selX, g_selY)) {
        g_b[g_selY][g_selX] = g_b[g_pickY][g_pickX];
        g_b[g_pickY][g_pickX] = 0;
        g_pickX = g_pickY = -1;
        g_turn = -1;
        Move tmp[120];
        if (gen_moves(-1, tmp, 120) == 0) g_over = 1;  // AI mated
      } else if (g_b[g_selY][g_selX] > 0) {
        g_pickX = g_selX;
        g_pickY = g_selY;  // reselect
      } else {
        g_pickX = g_pickY = -1;
      }
    }
    f = CP_LOOP_RENDER;
  }
  if (f == CP_LOOP_IDLE) api->delay_ms(30);
  return f;
}

static const char* piece_name(int p) {
  static const char* red[8] = {"", "車", "馬", "炮", "相", "仕", "帥", "兵"};
  static const char* blk[8] = {"", "車", "馬", "砲", "象", "士", "將", "卒"};
  const int t = p > 0 ? p : -p;
  return p > 0 ? red[t] : blk[t];
}

static void on_render(const CpApi* api) {
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == 0) {
    app_header(api, "中国象棋", "");
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 30, "你执红（下方）对战 AI", 1, CP_TEXT_BOLD);
    api->draw_text_centered(CP_FONT_UI, w / 2, h / 2 + 10, "确认开始", 1, CP_TEXT_REGULAR);
    app_hints(api, "返回", "开始", "", "");
    if (g_about.open) app_about_draw(api, "中国象棋");
    return;
  }

  const char* status = g_over == 1   ? "你赢了！"
                       : g_over == 2 ? "AI 获胜"
                                     : (g_turn == 1 ? (in_check(1) ? "将军！" : "你的回合") : "AI 思考…");
  const int top = app_header(api, "中国象棋", status);

  const int cell = (w - 20) / 9;
  const int bx = (w - cell * 9) / 2 + cell / 2;
  const int by = top + 8 + cell / 2;
  // grid
  for (int y = 0; y < 10; ++y) api->draw_line(bx, by + y * cell, bx + 8 * cell, by + y * cell, 1);
  for (int x = 0; x < 9; ++x) {
    api->draw_line(bx + x * cell, by, bx + x * cell, by + 4 * cell, 1);
    api->draw_line(bx + x * cell, by + 5 * cell, bx + x * cell, by + 9 * cell, 1);
  }
  api->draw_line(bx, by, bx, by + 9 * cell, 1);
  api->draw_line(bx + 8 * cell, by, bx + 8 * cell, by + 9 * cell, 1);
  // palaces
  api->draw_line(bx + 3 * cell, by, bx + 5 * cell, by + 2 * cell, 1);
  api->draw_line(bx + 5 * cell, by, bx + 3 * cell, by + 2 * cell, 1);
  api->draw_line(bx + 3 * cell, by + 7 * cell, bx + 5 * cell, by + 9 * cell, 1);
  api->draw_line(bx + 5 * cell, by + 7 * cell, bx + 3 * cell, by + 9 * cell, 1);

  const int pr = cell / 2 - 2;
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 9; ++x) {
      if (!g_b[y][x]) continue;
      const int cxp = bx + x * cell, cyp = by + y * cell;
      api->fill_rect(cxp - pr, cyp - pr, 2 * pr, 2 * pr, 0);  // white disc bg
      api->draw_rect(cxp - pr, cyp - pr, 2 * pr, 2 * pr, 1);
      const char* nm = piece_name(g_b[y][x]);
      const int tw = api->text_width(CP_FONT_UI, nm, CP_TEXT_BOLD);
      const int th = api->line_height(CP_FONT_UI);
      // Black pieces drawn with a filled ring to distinguish on 1-bit.
      if (g_b[y][x] < 0) api->draw_rect(cxp - pr + 2, cyp - pr + 2, 2 * pr - 4, 2 * pr - 4, 1);
      api->draw_text(CP_FONT_UI, cxp - tw / 2, cyp - th / 2, nm, 1, CP_TEXT_BOLD);
    }

  // cursor + picked
  api->draw_rect(bx + g_selX * cell - cell / 2, by + g_selY * cell - cell / 2, cell, cell, 1);
  if (g_pickX >= 0) {
    api->draw_rect(bx + g_pickX * cell - pr - 2, by + g_pickY * cell - pr - 2, 2 * pr + 4, 2 * pr + 4, 1);
  }

  if (g_over) api->draw_text_centered(CP_FONT_UI, w / 2, h - 40, "确认再来一局", 1, CP_TEXT_BOLD);
  app_hints(api, "返回", g_over ? "再来" : (g_pickX >= 0 ? "落子" : "选子"), "方向移动", "");
  if (g_about.open) app_about_draw(api, "中国象棋");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "中国象棋", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
