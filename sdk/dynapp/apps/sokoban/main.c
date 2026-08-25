// 推箱子 — Sokoban. Built-in levels, push boxes onto targets, undo, move
// counter, mid-level resume by re-entering. D-pad walks the worker.
// Ported to .eapp. Legend: # wall, @ worker, $ box, . target, * box-on-target,
// + worker-on-target, space floor.

#include "app.h"

#define MAXW 12
#define MAXH 12

static const char* kLevels[] = {
    // 1
    "########\n"
    "#  .   #\n"
    "# $$@  #\n"
    "#  .   #\n"
    "########",
    // 2
    "#######\n"
    "#. #  #\n"
    "#. $@ #\n"
    "#. $  #\n"
    "#. $  #\n"
    "#     #\n"
    "#######",
    // 3
    " ####\n"
    "## . ##\n"
    "#  $  #\n"
    "# #$# #\n"
    "# .@. #\n"
    "## $ ##\n"
    " #. #\n"
    " ####",
    // 4
    "########\n"
    "#      #\n"
    "# .$.$ #\n"
    "# $.@. #\n"
    "# .$.$ #\n"
    "# $.$. #\n"
    "#      #\n"
    "########",
};
#define NLEVELS (int)(sizeof(kLevels) / sizeof(kLevels[0]))

static char g_map[MAXH][MAXW];  // static walls + targets
static char g_box[MAXH][MAXW];  // 1 where a box sits
static int g_w, g_h;
static int g_px, g_py;
static int g_level;
static int g_moves;
static int g_done;
static int g_view;
static AppAbout g_about;

// Undo stack: (px,py, boxFromX,boxFromY or -1).
static struct {
  signed char px, py, bx, by;
} g_undo[128];
static int g_undoN;

static void parse_level(int idx) {
  memset(g_map, ' ', sizeof(g_map));
  memset(g_box, 0, sizeof(g_box));
  const char* s = kLevels[idx];
  int x = 0, y = 0;
  g_w = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '\n') {
      y++;
      x = 0;
      continue;
    }
    if (y < MAXH && x < MAXW) {
      const char c = *p;
      if (c == '#')
        g_map[y][x] = '#';
      else if (c == '.' || c == '*' || c == '+')
        g_map[y][x] = '.';
      else
        g_map[y][x] = ' ';
      if (c == '$' || c == '*') g_box[y][x] = 1;
      if (c == '@' || c == '+') {
        g_px = x;
        g_py = y;
      }
      x++;
      if (x > g_w) g_w = x;
    }
    y = y;  // keep
  }
  g_h = y + 1;
}

static void load_level(int idx) {
  g_level = idx;
  parse_level(idx);
  g_moves = 0;
  g_done = 0;
  g_undoN = 0;
}

static int is_wall(int x, int y) { return x < 0 || x >= g_w || y < 0 || y >= g_h || g_map[y][x] == '#'; }

static void check_done(void) {
  for (int y = 0; y < g_h; ++y)
    for (int x = 0; x < g_w; ++x)
      if (g_map[y][x] == '.' && !g_box[y][x]) return;
  g_done = 1;
}

static void step(int dx, int dy) {
  if (g_done) return;
  const int nx = g_px + dx, ny = g_py + dy;
  if (is_wall(nx, ny)) return;
  signed char rec_bx = -1, rec_by = -1;
  if (g_box[ny][nx]) {
    const int bx = nx + dx, by = ny + dy;
    if (is_wall(bx, by) || g_box[by][bx]) return;  // blocked
    g_box[ny][nx] = 0;
    g_box[by][bx] = 1;
    rec_bx = (signed char)nx;
    rec_by = (signed char)ny;
  }
  if (g_undoN < 128) {
    g_undo[g_undoN].px = (signed char)g_px;
    g_undo[g_undoN].py = (signed char)g_py;
    g_undo[g_undoN].bx = rec_bx;
    g_undo[g_undoN].by = rec_by;
    ++g_undoN;
  }
  g_px = nx;
  g_py = ny;
  ++g_moves;
  check_done();
}

static void undo(void) {
  if (g_undoN == 0) return;
  --g_undoN;
  const int oldpx = g_px, oldpy = g_py;
  g_px = g_undo[g_undoN].px;
  g_py = g_undo[g_undoN].py;
  if (g_undo[g_undoN].bx >= 0) {
    // box was pushed from (bx,by) into the worker's old spot; reverse it
    const int fromx = g_undo[g_undoN].bx, fromy = g_undo[g_undoN].by;
    const int boxx = oldpx + (oldpx - g_px), boxy = oldpy + (oldpy - g_py);
    g_box[boxy][boxx] = 0;
    g_box[fromy][fromx] = 1;
  }
  if (g_moves) --g_moves;
  g_done = 0;
}

static int32_t on_enter(const CpApi* api) {
  g_view = 0;
  g_level = 0;
  (void)api;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (g_view == 0) {
    if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
    int sel = g_level;
    if (app_menu_input(api, in, &sel, NLEVELS)) {
      load_level(sel);
      g_view = 1;
      return CP_LOOP_RENDER;
    }
    if (sel != g_level) {
      g_level = sel;
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }

  if (in->released & CP_BTN_BACK) {
    g_view = 0;
    return CP_LOOP_RENDER;
  }
  uint32_t f = CP_LOOP_IDLE;
  if (g_done) {
    if (in->released & CP_BTN_CONFIRM) {
      g_level = (g_level + 1) % NLEVELS;
      load_level(g_level);
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  if (in->released & CP_BTN_LEFT) {
    step(-1, 0);
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_RIGHT) {
    step(1, 0);
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_UP) {
    step(0, -1);
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_DOWN) {
    step(0, 1);
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_CONFIRM) {
    undo();
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_PAGE_FORWARD) {
    load_level(g_level);
    f = CP_LOOP_RENDER;
  }  // restart
  if (f == CP_LOOP_IDLE) api->delay_ms(40);
  return f;
}

static void on_render(const CpApi* api) {
  char buf[32];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == 0) {
    app_header(api, "推箱子", "选择关卡");
    static char lb[NLEVELS][20];
    static const char* pt[NLEVELS];
    for (int i = 0; i < NLEVELS; ++i) {
      cp_snprintf(lb[i], sizeof(lb[0]), "第 %d 关", i + 1);
      pt[i] = lb[i];
    }
    app_menu_draw(api, 60, pt, NLEVELS, g_level);
    app_hints(api, "返回", "开始", "上下选择", "");
    if (g_about.open) app_about_draw(api, "推箱子");
    return;
  }

  cp_snprintf(buf, sizeof(buf), "第%d关·%d步", g_level + 1, g_moves);
  const int top = app_header(api, "推箱子", buf);

  int cell = (w - 24) / (g_w > 0 ? g_w : 1);
  const int maxCellH = (h - top - 60) / (g_h > 0 ? g_h : 1);
  if (cell > maxCellH) cell = maxCellH;
  if (cell < 8) cell = 8;
  const int bx = (w - cell * g_w) / 2;
  const int by = top + 12;

  for (int y = 0; y < g_h; ++y)
    for (int x = 0; x < g_w; ++x) {
      const int px = bx + x * cell, py = by + y * cell;
      if (g_map[y][x] == '#') {
        api->fill_rect(px, py, cell, cell, 1);
      } else if (g_map[y][x] == '.') {
        api->draw_rect(px + cell / 3, py + cell / 3, cell / 3, cell / 3, 1);
      }
      if (g_box[y][x]) {
        const int on = (g_map[y][x] == '.');
        if (on)
          api->fill_rect(px + 3, py + 3, cell - 6, cell - 6, 1);
        else {
          api->draw_rect(px + 3, py + 3, cell - 6, cell - 6, 1);
          api->draw_line(px + 3, py + 3, px + cell - 3, py + cell - 3, 1);
        }
      }
      if (x == g_px && y == g_py) {
        api->draw_rect(px + 2, py + 2, cell - 4, cell - 4, 1);
        api->fill_rect(px + cell / 3, py + cell / 4, cell / 3, cell / 3, 1);  // head
      }
    }

  if (g_done) api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h - 56, "过关！确认下一关", 1, CP_TEXT_BOLD);
  app_hints(api, "返回", g_done ? "下一关" : "悔一步", "方向移动", "翻页=重来");
  if (g_about.open) app_about_draw(api, "推箱子");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "推箱子", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
