// 数独 — Sudoku. Backtracking generator with three difficulties (by number
// of givens), pencil-free classic play. D-pad moves, 1-9 chosen by up/down on
// the cell, Confirm cycles value. Ported to .eapp.

#include "app.h"

static unsigned char g_sol[9][9];    // full solution
static unsigned char g_grid[9][9];   // current
static unsigned char g_given[9][9];  // fixed cells
static int g_cx, g_cy;
static int g_diff;
static int g_done;
static int g_view;
static AppAbout g_about;

static const char* kDiffName[3] = {"简单", "普通", "困难"};
static const int kGivens[3] = {40, 32, 26};

static int ok_at(unsigned char g[9][9], int r, int c, int v) {
  for (int i = 0; i < 9; ++i) {
    if (g[r][i] == v || g[i][c] == v) return 0;
  }
  const int br = (r / 3) * 3, bc = (c / 3) * 3;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      if (g[br + i][bc + j] == v) return 0;
  return 1;
}

// Fill a complete valid grid via randomized backtracking.
static int fill_grid(const CpApi* api, int pos) {
  if (pos == 81) return 1;
  const int r = pos / 9, c = pos % 9;
  int nums[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 8; i > 0; --i) {  // shuffle
    const int j = api->random_u32() % (i + 1);
    const int t = nums[i];
    nums[i] = nums[j];
    nums[j] = t;
  }
  for (int i = 0; i < 9; ++i) {
    if (ok_at(g_sol, r, c, nums[i])) {
      g_sol[r][c] = nums[i];
      if (fill_grid(api, pos + 1)) return 1;
      g_sol[r][c] = 0;
    }
  }
  return 0;
}

static void new_game(const CpApi* api, int d) {
  g_diff = d;
  memset(g_sol, 0, sizeof(g_sol));
  fill_grid(api, 0);
  memcpy(g_grid, g_sol, sizeof(g_grid));
  // Remove cells down to the target given-count.
  int remove = 81 - kGivens[d];
  int guard = 0;
  while (remove > 0 && guard < 4000) {
    ++guard;
    const int r = api->random_u32() % 9, c = api->random_u32() % 9;
    if (g_grid[r][c] == 0) continue;
    g_grid[r][c] = 0;
    --remove;
  }
  for (int r = 0; r < 9; ++r)
    for (int c = 0; c < 9; ++c) g_given[r][c] = (g_grid[r][c] != 0);
  g_cx = g_cy = 0;
  g_done = 0;
}

static void check_done(void) {
  for (int r = 0; r < 9; ++r)
    for (int c = 0; c < 9; ++c)
      if (g_grid[r][c] != g_sol[r][c]) return;
  g_done = 1;
}

static int32_t on_enter(const CpApi* api) {
  g_view = 0;
  g_diff = 0;
  (void)api;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (g_view == 0) {
    if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
    int sel = g_diff;
    if (app_menu_input(api, in, &sel, 3)) {
      app_message(api, "生成中…");
      new_game(api, sel);
      g_view = 1;
      return CP_LOOP_RENDER;
    }
    if (sel != g_diff) {
      g_diff = sel;
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }

  if (in->released & CP_BTN_BACK) {
    g_view = 0;
    return CP_LOOP_RENDER;
  }
  if (g_done) {
    if (in->released & CP_BTN_CONFIRM) {
      app_message(api, "生成中…");
      new_game(api, g_diff);
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  uint32_t f = CP_LOOP_IDLE;
  // Move cursor with page keys to keep up/down for value entry.
  if (in->released & CP_BTN_LEFT) {
    g_cx = (g_cx + 8) % 9;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_RIGHT) {
    g_cx = (g_cx + 1) % 9;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_PAGE_BACK) {
    g_cy = (g_cy + 8) % 9;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_PAGE_FORWARD) {
    g_cy = (g_cy + 1) % 9;
    f = CP_LOOP_RENDER;
  } else if (!g_given[g_cy][g_cx] && (in->released & CP_BTN_UP)) {
    g_grid[g_cy][g_cx] = (g_grid[g_cy][g_cx] + 1) % 10;
    f = CP_LOOP_RENDER;
  } else if (!g_given[g_cy][g_cx] && (in->released & CP_BTN_DOWN)) {
    g_grid[g_cy][g_cx] = (g_grid[g_cy][g_cx] + 9) % 10;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_CONFIRM) {
    check_done();
    f = CP_LOOP_RENDER;
  }
  if (f == CP_LOOP_IDLE) api->delay_ms(40);
  return f;
}

static void on_render(const CpApi* api) {
  char buf[8];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == 0) {
    app_header(api, "数独", "选择难度");
    static char lb[3][24];
    static const char* pt[3];
    for (int i = 0; i < 3; ++i) {
      cp_snprintf(lb[i], sizeof(lb[0]), "%s（%d 提示）", kDiffName[i], kGivens[i]);
      pt[i] = lb[i];
    }
    app_menu_draw(api, 60, pt, 3, g_diff);
    app_hints(api, "返回", "开始", "上下选择", "");
    if (g_about.open) app_about_draw(api, "数独");
    return;
  }

  const int top = app_header(api, kDiffName[g_diff], g_done ? "完成！" : "");
  const int cell = (w - 20) / 9;
  const int bx = (w - cell * 9) / 2;
  const int by = top + 12;
  for (int r = 0; r <= 9; ++r) {
    const int lw = (r % 3 == 0) ? 2 : 1;
    for (int t = 0; t < lw; ++t) {
      api->draw_line(bx, by + r * cell + t, bx + 9 * cell, by + r * cell + t, 1);
      api->draw_line(bx + r * cell + t, by, bx + r * cell + t, by + 9 * cell, 1);
    }
  }
  for (int r = 0; r < 9; ++r)
    for (int c = 0; c < 9; ++c) {
      if (r == g_cy && c == g_cx) api->draw_rect(bx + c * cell + 2, by + r * cell + 2, cell - 4, cell - 4, 1);
      if (g_grid[r][c]) {
        cp_snprintf(buf, sizeof(buf), "%d", g_grid[r][c]);
        const int tw = api->text_width(CP_FONT_UI_LARGE, buf, g_given[r][c] ? CP_TEXT_BOLD : CP_TEXT_REGULAR);
        const int tx = bx + c * cell + (cell - tw) / 2;
        const int ty = by + r * cell + (cell - api->line_height(CP_FONT_UI_LARGE)) / 2;
        api->draw_text(CP_FONT_UI_LARGE, tx, ty, buf, 1, g_given[r][c] ? CP_TEXT_BOLD : CP_TEXT_REGULAR);
      }
    }
  if (g_done) api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h - 58, "恭喜完成！确认再来", 1, CP_TEXT_BOLD);
  app_hints(api, "返回", g_done ? "再来" : "校验", "左右移动", "翻页=行");
  if (g_about.open) app_about_draw(api, "数独");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "数独", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
