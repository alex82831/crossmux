// 扫雷 — Minesweeper. Three difficulties, flood reveal, flags, win/lose,
// best-time per difficulty. D-pad cursor, Confirm reveals, a hold/second key
// flags. Ported to .eapp.

#include "app.h"

#define MAXW 12
#define MAXH 16

typedef struct {
  const char* name;
  int w, h, mines;
} Diff;
static const Diff kDiff[3] = {
    {"简单", 8, 10, 10},
    {"普通", 10, 13, 22},
    {"困难", 12, 16, 40},
};

static int g_diff;
static int g_w, g_h, g_mines;
static unsigned char g_mine[MAXH][MAXW];
static unsigned char g_open[MAXH][MAXW];
static unsigned char g_flag[MAXH][MAXW];
static int g_cx, g_cy;
static int g_state;  // 0=play 1=won 2=lost
static int g_started;
static uint32_t g_start_ms;
static int g_view;  // 0=menu 1=play
static AppAbout g_about;

static int neighbors(int x, int y) {
  int n = 0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      const int nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < g_w && ny >= 0 && ny < g_h && g_mine[ny][nx]) ++n;
    }
  return n;
}

static void new_game(const CpApi* api, int d) {
  g_diff = d;
  g_w = kDiff[d].w;
  g_h = kDiff[d].h;
  g_mines = kDiff[d].mines;
  memset(g_mine, 0, sizeof(g_mine));
  memset(g_open, 0, sizeof(g_open));
  memset(g_flag, 0, sizeof(g_flag));
  g_cx = g_w / 2;
  g_cy = g_h / 2;
  g_state = 0;
  g_started = 0;
  (void)api;
}

static void place_mines(const CpApi* api, int safeX, int safeY) {
  int placed = 0;
  while (placed < g_mines) {
    const int x = api->random_u32() % g_w;
    const int y = api->random_u32() % g_h;
    if (g_mine[y][x]) continue;
    if (x >= safeX - 1 && x <= safeX + 1 && y >= safeY - 1 && y <= safeY + 1) continue;  // safe first click
    g_mine[y][x] = 1;
    ++placed;
  }
  g_started = 1;
  g_start_ms = api->millis();
}

static void flood(int x, int y) {
  if (x < 0 || x >= g_w || y < 0 || y >= g_h) return;
  if (g_open[y][x] || g_flag[y][x]) return;
  g_open[y][x] = 1;
  if (neighbors(x, y) != 0) return;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      if (dx || dy) flood(x + dx, y + dy);
}

static void check_win(void) {
  int closed = 0;
  for (int y = 0; y < g_h; ++y)
    for (int x = 0; x < g_w; ++x)
      if (!g_open[y][x]) ++closed;
  if (closed == g_mines) g_state = 1;
}

static void reveal(const CpApi* api) {
  if (g_state) return;
  if (g_flag[g_cy][g_cx]) return;
  if (!g_started) place_mines(api, g_cx, g_cy);
  if (g_mine[g_cy][g_cx]) {
    g_state = 2;
    for (int y = 0; y < g_h; ++y)
      for (int x = 0; x < g_w; ++x)
        if (g_mine[y][x]) g_open[y][x] = 1;
    return;
  }
  flood(g_cx, g_cy);
  check_win();
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
  uint32_t f = CP_LOOP_IDLE;
  if (g_state) {
    if (in->released & CP_BTN_CONFIRM) {
      new_game(api, g_diff);
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  if (in->released & CP_BTN_LEFT) {
    g_cx = (g_cx + g_w - 1) % g_w;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_RIGHT) {
    g_cx = (g_cx + 1) % g_w;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_UP) {
    g_cy = (g_cy + g_h - 1) % g_h;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_DOWN) {
    g_cy = (g_cy + 1) % g_h;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_CONFIRM) {
    reveal(api);
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_PAGE_FORWARD) {  // flag toggle
    if (!g_open[g_cy][g_cx]) g_flag[g_cy][g_cx] ^= 1;
    f = CP_LOOP_RENDER;
  }
  if (f == CP_LOOP_IDLE) api->delay_ms(40);
  return f;
}

static void on_render(const CpApi* api) {
  char buf[40];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == 0) {
    app_header(api, "扫雷", "选择难度");
    static char lb[3][32];
    static const char* pt[3];
    for (int i = 0; i < 3; ++i) {
      cp_snprintf(lb[i], sizeof(lb[0]), "%s（%dx%d·%d雷）", kDiff[i].name, kDiff[i].w, kDiff[i].h, kDiff[i].mines);
      pt[i] = lb[i];
    }
    app_menu_draw(api, 60, pt, 3, g_diff);
    app_hints(api, "返回", "开始", "上下选择", "");
    if (g_about.open) app_about_draw(api, "扫雷");
    return;
  }

  int flags = 0;
  for (int y = 0; y < g_h; ++y)
    for (int x = 0; x < g_w; ++x)
      if (g_flag[y][x]) ++flags;
  cp_snprintf(buf, sizeof(buf), "雷 %d/%d", flags, g_mines);
  const int top = app_header(api, kDiff[g_diff].name, buf);

  const int cell = (w - 24) / g_w;
  const int bx = (w - cell * g_w) / 2;
  const int by = top + 12;
  for (int y = 0; y < g_h; ++y)
    for (int x = 0; x < g_w; ++x) {
      const int px = bx + x * cell, py = by + y * cell;
      if (g_open[y][x]) {
        if (g_mine[y][x]) {
          api->fill_rect(px + 1, py + 1, cell - 2, cell - 2, 1);
        } else {
          api->draw_rect(px, py, cell, cell, 1);
          const int n = neighbors(x, y);
          if (n) {
            cp_snprintf(buf, sizeof(buf), "%d", n);
            const int tw = api->text_width(CP_FONT_UI, buf, CP_TEXT_BOLD);
            api->draw_text(CP_FONT_UI, px + (cell - tw) / 2, py + (cell - api->line_height(CP_FONT_UI)) / 2, buf, 1,
                           CP_TEXT_BOLD);
          }
        }
      } else {
        api->fill_rect(px + 1, py + 1, cell - 2, cell - 2, 0);
        api->draw_rect(px, py, cell, cell, 1);
        if (g_flag[y][x]) api->fill_rect(px + cell / 4, py + cell / 4, cell / 2, cell / 2, 1);
      }
    }
  // Cursor
  api->draw_rect(bx + g_cx * cell - 1, by + g_cy * cell - 1, cell + 2, cell + 2, 1);
  api->draw_rect(bx + g_cx * cell + 1, by + g_cy * cell + 1, cell - 2, cell - 2, 1);

  if (g_state == 1)
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h - 60, "胜利！确认重来", 1, CP_TEXT_BOLD);
  else if (g_state == 2)
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h - 60, "踩雷了 · 确认重来", 1, CP_TEXT_BOLD);

  app_hints(api, "返回", g_state ? "重来" : "翻开", "方向移动", "翻页=旗");
  if (g_about.open) app_about_draw(api, "扫雷");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "扫雷", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
