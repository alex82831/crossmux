// 五子棋 — Gomoku vs a heuristic AI on a 15x15 board. Threat-based scoring
// (open/closed twos/threes/fours). D-pad moves the cursor, Confirm places.
// Ported to .eapp.

#include "app.h"

#define BN 15
static signed char g_b[BN][BN];  // 0 empty, 1 human(black), 2 ai(white)
static int g_cx, g_cy;
static int g_turn;  // 1=human 2=ai
static int g_winner;
static int g_view;
static AppAbout g_about;

static const int DX[4] = {1, 0, 1, 1};
static const int DY[4] = {0, 1, 1, -1};

static int in_b(int x, int y) { return x >= 0 && x < BN && y >= 0 && y < BN; }

static int wins_at(int who, int x, int y) {
  for (int d = 0; d < 4; ++d) {
    int cnt = 1;
    for (int s = 1; s < 5; ++s) { int nx = x + DX[d] * s, ny = y + DY[d] * s; if (in_b(nx, ny) && g_b[ny][nx] == who) ++cnt; else break; }
    for (int s = 1; s < 5; ++s) { int nx = x - DX[d] * s, ny = y - DY[d] * s; if (in_b(nx, ny) && g_b[ny][nx] == who) ++cnt; else break; }
    if (cnt >= 5) return 1;
  }
  return 0;
}

// Score a hypothetical stone for `who` at (x,y): sum of line potentials.
static long line_score(int who, int x, int y) {
  long score = 0;
  for (int d = 0; d < 4; ++d) {
    int cnt = 1, openEnds = 0;
    int nx = x + DX[d], ny = y + DY[d];
    while (in_b(nx, ny) && g_b[ny][nx] == who) { ++cnt; nx += DX[d]; ny += DY[d]; }
    if (in_b(nx, ny) && g_b[ny][nx] == 0) ++openEnds;
    nx = x - DX[d]; ny = y - DY[d];
    while (in_b(nx, ny) && g_b[ny][nx] == who) { ++cnt; nx -= DX[d]; ny -= DY[d]; }
    if (in_b(nx, ny) && g_b[ny][nx] == 0) ++openEnds;
    if (cnt >= 5) score += 1000000;
    else if (cnt == 4) score += openEnds == 2 ? 100000 : (openEnds == 1 ? 12000 : 0);
    else if (cnt == 3) score += openEnds == 2 ? 8000 : (openEnds == 1 ? 800 : 0);
    else if (cnt == 2) score += openEnds == 2 ? 400 : (openEnds == 1 ? 60 : 0);
    else if (cnt == 1) score += openEnds == 2 ? 20 : 5;
  }
  return score;
}

static int has_neighbor(int x, int y) {
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx) {
      if (!dx && !dy) continue;
      const int nx = x + dx, ny = y + dy;
      if (in_b(nx, ny) && g_b[ny][nx]) return 1;
    }
  return 0;
}

static void ai_move(const CpApi* api) {
  long best = -1;
  int bx = BN / 2, by = BN / 2;
  int found = 0;
  for (int y = 0; y < BN; ++y)
    for (int x = 0; x < BN; ++x) {
      if (g_b[y][x]) continue;
      if (!has_neighbor(x, y)) continue;
      // Value = own offense + slightly-discounted block of the human.
      const long off = line_score(2, x, y);
      const long def = line_score(1, x, y);
      long v = off + def * 9 / 10;
      // tiny jitter to avoid deterministic ties
      v += api->random_u32() % 7;
      if (v > best) { best = v; bx = x; by = y; found = 1; }
    }
  if (!found) {  // empty board: center
    bx = BN / 2; by = BN / 2;
  }
  g_b[by][bx] = 2;
  if (wins_at(2, bx, by)) g_winner = 2;
  g_turn = 1;
}

static void reset(void) {
  memset(g_b, 0, sizeof(g_b));
  g_cx = g_cy = BN / 2;
  g_turn = 1;
  g_winner = 0;
}

static int32_t on_enter(const CpApi* api) {
  (void)api;
  g_view = 0;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (g_view == 0) {  // simple start screen
    if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
    if (in->released & CP_BTN_CONFIRM) { reset(); g_view = 1; return CP_LOOP_RENDER; }
    return CP_LOOP_IDLE;
  }

  if (in->released & CP_BTN_BACK) { g_view = 0; return CP_LOOP_RENDER; }
  uint32_t f = CP_LOOP_IDLE;
  if (g_winner) {
    if (in->released & CP_BTN_CONFIRM) { reset(); return CP_LOOP_RENDER; }
    return CP_LOOP_IDLE;
  }
  if (g_turn == 1) {
    if (in->released & CP_BTN_LEFT) { g_cx = (g_cx + BN - 1) % BN; f = CP_LOOP_RENDER; }
    else if (in->released & CP_BTN_RIGHT) { g_cx = (g_cx + 1) % BN; f = CP_LOOP_RENDER; }
    else if (in->released & CP_BTN_UP) { g_cy = (g_cy + BN - 1) % BN; f = CP_LOOP_RENDER; }
    else if (in->released & CP_BTN_DOWN) { g_cy = (g_cy + 1) % BN; f = CP_LOOP_RENDER; }
    else if (in->released & CP_BTN_CONFIRM) {
      if (g_b[g_cy][g_cx] == 0) {
        g_b[g_cy][g_cx] = 1;
        if (wins_at(1, g_cx, g_cy)) g_winner = 1;
        else g_turn = 2;
      }
      f = CP_LOOP_RENDER;
    }
  } else {
    // AI thinks (blocking, fast enough for 15x15 single-ply).
    ai_move(api);
    f = CP_LOOP_RENDER;
  }
  if (f == CP_LOOP_IDLE) api->delay_ms(30);
  return f;
}

static void on_render(const CpApi* api) {
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == 0) {
    app_header(api, "五子棋", "");
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 30, "黑先 · 你执黑对战 AI", 1, CP_TEXT_BOLD);
    api->draw_text_centered(CP_FONT_UI, w / 2, h / 2 + 10, "确认开始", 1, CP_TEXT_REGULAR);
    app_hints(api, "返回", "开始", "", "");
    if (g_about.open) app_about_draw(api, "五子棋");
    return;
  }

  const char* status = g_winner == 1 ? "你赢了！" : g_winner == 2 ? "AI 获胜" : (g_turn == 1 ? "你的回合" : "AI 思考…");
  const int top = app_header(api, "五子棋", status);

  const int cell = (w - 20) / BN;
  const int bx = (w - cell * BN) / 2 + cell / 2;
  const int by = top + 10 + cell / 2;
  // grid lines
  for (int i = 0; i < BN; ++i) {
    api->draw_line(bx, by + i * cell, bx + (BN - 1) * cell, by + i * cell, 1);
    api->draw_line(bx + i * cell, by, bx + i * cell, by + (BN - 1) * cell, 1);
  }
  for (int y = 0; y < BN; ++y)
    for (int x = 0; x < BN; ++x)
      if (g_b[y][x]) {
        const int cxp = bx + x * cell, cyp = by + y * cell;
        const int r = cell / 2 - 1;
        if (g_b[y][x] == 1) {
          for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) if (dx * dx + dy * dy <= r * r) api->draw_pixel(cxp + dx, cyp + dy, 1);
        } else {
          for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) if (dx * dx + dy * dy <= r * r && dx * dx + dy * dy >= (r - 1) * (r - 1)) api->draw_pixel(cxp + dx, cyp + dy, 1);
          api->fill_rect(cxp - r / 2, cyp - r / 2, r, r, 0);
        }
      }
  if (!g_winner && g_turn == 1) {
    const int cxp = bx + g_cx * cell, cyp = by + g_cy * cell;
    api->draw_rect(cxp - cell / 2, cyp - cell / 2, cell, cell, 1);
  }

  if (g_winner) api->draw_text_centered(CP_FONT_UI, w / 2, h - 54, "确认再来一局", 1, CP_TEXT_BOLD);
  app_hints(api, "返回", g_winner ? "再来" : "落子", "方向移动", "");
  if (g_about.open) app_about_draw(api, "五子棋");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "五子棋", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
