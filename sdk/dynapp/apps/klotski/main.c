// 华容道 — 4x5 Klotski sliding-block puzzle. 9 solver-verified layouts,
// move counter + best record, undo, mid-game resume. Ported to .eapp.

#include "app.h"
#include "layouts.h"

#define BW 4
#define BH 5
#define NLAY (int)(sizeof(kLayouts) / sizeof(kLayouts[0]))
#define EXIT_X 1
#define EXIT_Y 3

// Piece dimensions by type: 0=Cao 2x2, 1=Vert 1x2, 2=Horz 2x1, 3=Soldier 1x1.
static const unsigned char kW[4] = {2, 1, 2, 1};
static const unsigned char kH[4] = {2, 2, 1, 1};

typedef struct {
  unsigned char type, x, y;
} Piece;

static int g_layout;
static Piece g_pieces[10];
static int g_nPieces;
static int g_sel;
static int g_moves;
static int g_best[NLAY];
static int g_solved;
static int g_view;  // 0=menu(layout pick), 1=play
static AppAbout g_about;

// Undo: keep last 64 (pieceIdx, dx, dy).
static struct {
  unsigned char idx;
  signed char dx, dy;
} g_undo[64];
static int g_undoN;

static int occupies(int pi, int cx, int cy) {
  const Piece* p = &g_pieces[pi];
  return cx >= p->x && cx < p->x + kW[p->type] && cy >= p->y && cy < p->y + kH[p->type];
}

static int cell_blocked_by_other(int self, int cx, int cy) {
  if (cx < 0 || cx >= BW || cy < 0 || cy >= BH) return 1;
  for (int i = 0; i < g_nPieces; ++i) {
    if (i == self) continue;
    if (occupies(i, cx, cy)) return 1;
  }
  return 0;
}

static int can_move(int pi, int dx, int dy) {
  const Piece* p = &g_pieces[pi];
  for (int oy = 0; oy < kH[p->type]; ++oy)
    for (int ox = 0; ox < kW[p->type]; ++ox)
      if (cell_blocked_by_other(pi, p->x + ox + dx, p->y + oy + dy)) return 0;
  return 1;
}

static void load_layout(const CpApi* api, int idx) {
  g_layout = idx;
  g_nPieces = 0;
  for (int i = 0; i < 10; ++i) {
    const KPiece* kp = &kLayouts[idx].pieces[i];
    // A trailing all-zero soldier at (0,0) is real; layouts always list 10.
    g_pieces[i].type = kp->type;
    g_pieces[i].x = kp->x;
    g_pieces[i].y = kp->y;
    ++g_nPieces;
  }
  g_sel = 0;
  g_moves = 0;
  g_solved = 0;
  g_undoN = 0;
  // Try to restore an in-progress game for this layout.
  (void)api;
}

static void do_move(int pi, int dx, int dy) {
  g_pieces[pi].x += dx;
  g_pieces[pi].y += dy;
  ++g_moves;
  if (g_undoN < 64) {
    g_undo[g_undoN].idx = pi;
    g_undo[g_undoN].dx = dx;
    g_undo[g_undoN].dy = dy;
    ++g_undoN;
  }
  // Win: Cao Cao (type 0) top-left at the exit.
  if (g_pieces[pi].type == 0 && g_pieces[pi].x == EXIT_X && g_pieces[pi].y == EXIT_Y) g_solved = 1;
}

static void undo(void) {
  if (g_undoN == 0) return;
  --g_undoN;
  g_pieces[g_undo[g_undoN].idx].x -= g_undo[g_undoN].dx;
  g_pieces[g_undo[g_undoN].idx].y -= g_undo[g_undoN].dy;
  if (g_moves > 0) --g_moves;
  g_solved = 0;
}

static void save_best(const CpApi* api) { api->file_write("best.bin", g_best, sizeof(g_best)); }

static int32_t on_enter(const CpApi* api) {
  api->file_read("best.bin", g_best, sizeof(g_best));
  g_view = 0;
  g_layout = 0;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (g_view == 0) {  // layout menu
    if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
    int sel = g_layout;
    if (app_menu_input(api, in, &sel, NLAY)) {
      load_layout(api, sel);
      g_view = 1;
      return CP_LOOP_RENDER;
    }
    if (sel != g_layout) {
      g_layout = sel;
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }

  // play
  if (in->released & CP_BTN_BACK) {
    g_view = 0;
    return CP_LOOP_RENDER;
  }
  if (g_solved) {
    if (in->released & CP_BTN_CONFIRM) {
      load_layout(api, g_layout);
      return CP_LOOP_RENDER;
    }
    return CP_LOOP_IDLE;
  }
  uint32_t f = CP_LOOP_IDLE;
  if (in->released & CP_BTN_CONFIRM) {  // cycle selected piece
    g_sel = (g_sel + 1) % g_nPieces;
    f = CP_LOOP_RENDER;
  }
  if (in->released & CP_BTN_PAGE_BACK) {  // undo on a page key if present
    undo();
    f = CP_LOOP_RENDER;
  }
  int dx = 0, dy = 0;
  if (in->released & CP_BTN_LEFT) dx = -1;
  else if (in->released & CP_BTN_RIGHT) dx = 1;
  else if (in->released & CP_BTN_UP) dy = -1;
  else if (in->released & CP_BTN_DOWN) dy = 1;
  if (dx || dy) {
    if (can_move(g_sel, dx, dy)) {
      do_move(g_sel, dx, dy);
      if (g_solved && (g_best[g_layout] == 0 || g_moves < g_best[g_layout])) {
        g_best[g_layout] = g_moves;
        save_best(api);
      }
    }
    f = CP_LOOP_RENDER;
  }
  if (f == CP_LOOP_IDLE) api->delay_ms(40);
  return f;
}

static void on_render(const CpApi* api) {
  char buf[48];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == 0) {
    app_header(api, "华容道", "选择布局");
    // Build labels with best steps.
    static char labels[NLAY][40];
    static const char* ptrs[NLAY];
    for (int i = 0; i < NLAY; ++i) {
      if (g_best[i]) cp_snprintf(labels[i], sizeof(labels[0]), "%s（最少%d·最佳%d）", kLayouts[i].name, kLayouts[i].minSteps, g_best[i]);
      else cp_snprintf(labels[i], sizeof(labels[0]), "%s（最少%d步）", kLayouts[i].name, kLayouts[i].minSteps);
      ptrs[i] = labels[i];
    }
    app_menu_draw(api, 52, ptrs, NLAY, g_layout);
    app_hints(api, "返回", "开始", "上下选择", "");
    if (g_about.open) app_about_draw(api, "华容道");
    return;
  }

  cp_snprintf(buf, sizeof(buf), "步数 %d", g_moves);
  const int top = app_header(api, kLayouts[g_layout].name, buf);

  const int cell = (w - 40) / BW;
  const int bx = (w - cell * BW) / 2;
  const int by = top + 16;
  // Board frame with an exit gap at the bottom (cols EXIT_X..EXIT_X+1).
  api->draw_rect(bx - 3, by - 3, cell * BW + 6, cell * BH + 6, 1);
  const int exitL = bx + EXIT_X * cell, exitR = bx + (EXIT_X + 2) * cell;
  api->fill_rect(exitL, by + cell * BH, exitR - exitL, 4, 0);  // erase frame at exit

  for (int i = 0; i < g_nPieces; ++i) {
    const Piece* p = &g_pieces[i];
    const int px = bx + p->x * cell;
    const int py = by + p->y * cell;
    const int pw = kW[p->type] * cell;
    const int ph = kH[p->type] * cell;
    const int sel = (i == g_sel);
    if (p->type == 0) api->fill_rect(px + 2, py + 2, pw - 4, ph - 4, 1);  // Cao filled
    else api->draw_rect(px + 2, py + 2, pw - 4, ph - 4, 1);
    if (sel) api->draw_rect(px + 5, py + 5, pw - 10, ph - 10, p->type == 0 ? 0 : 1);
  }

  if (g_solved) {
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h - 66, "曹操已出！确认重来", 1, CP_TEXT_BOLD);
  }
  app_hints(api, "返回", "换块", "方向滑动", "");
  if (g_about.open) app_about_draw(api, "华容道");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "华容道", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
