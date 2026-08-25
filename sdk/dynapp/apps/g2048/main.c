// 2048 — classic 4x4 sliding tile game. Ported to .eapp.

#include "app.h"

#define N 4
static int g_board[N][N];
static uint32_t g_score, g_best;
static int g_over, g_won;
static AppAbout g_about;

static void spawn(const CpApi* api) {
  int empties[N * N][2], n = 0;
  for (int r = 0; r < N; ++r)
    for (int c = 0; c < N; ++c)
      if (g_board[r][c] == 0) {
        empties[n][0] = r;
        empties[n][1] = c;
        ++n;
      }
  if (n == 0) return;
  const int k = api->random_u32() % n;
  g_board[empties[k][0]][empties[k][1]] = (api->random_u32() % 10 == 0) ? 4 : 2;
}

static void reset(const CpApi* api) {
  for (int r = 0; r < N; ++r)
    for (int c = 0; c < N; ++c) g_board[r][c] = 0;
  g_score = 0;
  g_over = g_won = 0;
  spawn(api);
  spawn(api);
}

// Slide+merge one row toward index 0. Returns 1 if it changed.
static int slide_row(int* row) {
  int tmp[N], n = 0, changed = 0;
  for (int i = 0; i < N; ++i)
    if (row[i]) tmp[n++] = row[i];
  int out[N], m = 0;
  for (int i = 0; i < n; ++i) {
    if (i + 1 < n && tmp[i] == tmp[i + 1]) {
      out[m] = tmp[i] * 2;
      g_score += out[m];
      if (out[m] == 2048) g_won = 1;
      ++m;
      ++i;
    } else {
      out[m++] = tmp[i];
    }
  }
  while (m < N) out[m++] = 0;
  for (int i = 0; i < N; ++i) {
    if (row[i] != out[i]) changed = 1;
    row[i] = out[i];
  }
  return changed;
}

// dir: 0=left 1=right 2=up 3=down. Returns 1 if the board changed.
static int move(int dir) {
  int changed = 0;
  for (int i = 0; i < N; ++i) {
    int line[N];
    for (int j = 0; j < N; ++j) {
      int r, c;
      if (dir == 0) { r = i; c = j; }
      else if (dir == 1) { r = i; c = N - 1 - j; }
      else if (dir == 2) { r = j; c = i; }
      else { r = N - 1 - j; c = i; }
      line[j] = g_board[r][c];
    }
    if (slide_row(line)) changed = 1;
    for (int j = 0; j < N; ++j) {
      int r, c;
      if (dir == 0) { r = i; c = j; }
      else if (dir == 1) { r = i; c = N - 1 - j; }
      else if (dir == 2) { r = j; c = i; }
      else { r = N - 1 - j; c = i; }
      g_board[r][c] = line[j];
    }
  }
  return changed;
}

static int has_move(void) {
  for (int r = 0; r < N; ++r)
    for (int c = 0; c < N; ++c) {
      if (g_board[r][c] == 0) return 1;
      if (c + 1 < N && g_board[r][c] == g_board[r][c + 1]) return 1;
      if (r + 1 < N && g_board[r][c] == g_board[r + 1][c]) return 1;
    }
  return 0;
}

static void do_move(const CpApi* api, int dir) {
  if (g_over) return;
  if (move(dir)) {
    spawn(api);
    if (g_score > g_best) {
      g_best = g_score;
      api->file_write("best.bin", &g_best, sizeof(g_best));
    }
    if (!has_move()) g_over = 1;
  }
}

static int32_t on_enter(const CpApi* api) {
  api->file_read("best.bin", &g_best, sizeof(g_best));
  reset(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  uint32_t f = CP_LOOP_IDLE;
  if (g_over && (in->released & CP_BTN_CONFIRM)) {
    reset(api);
    return CP_LOOP_RENDER;
  }
  if (in->released & CP_BTN_LEFT) { do_move(api, 0); f = CP_LOOP_RENDER; }
  else if (in->released & CP_BTN_RIGHT) { do_move(api, 1); f = CP_LOOP_RENDER; }
  else if (in->released & CP_BTN_UP) { do_move(api, 2); f = CP_LOOP_RENDER; }
  else if (in->released & CP_BTN_DOWN) { do_move(api, 3); f = CP_LOOP_RENDER; }
  if (f == CP_LOOP_IDLE) api->delay_ms(40);
  return f;
}

static void on_render(const CpApi* api) {
  char buf[40];
  const int w = api->screen_width();
  cp_snprintf(buf, sizeof(buf), "分 %u  最佳 %u", g_score, g_best);
  const int top = app_header(api, "2048", buf);

  const int margin = 16;
  const int grid = w - 2 * margin;
  const int cell = grid / N;
  const int gy = top + 20;
  api->draw_rect(margin - 2, gy - 2, cell * N + 4, cell * N + 4, 1);
  for (int r = 0; r < N; ++r)
    for (int c = 0; c < N; ++c) {
      const int x = margin + c * cell;
      const int y = gy + r * cell;
      api->draw_rect(x, y, cell, cell, 1);
      if (g_board[r][c]) {
        // Higher tiles: filled background, inverted number.
        const int filled = g_board[r][c] >= 16;
        if (filled) api->fill_rect(x + 2, y + 2, cell - 4, cell - 4, 1);
        cp_snprintf(buf, sizeof(buf), "%d", g_board[r][c]);
        const int tw = api->text_width(CP_FONT_UI_LARGE, buf, CP_TEXT_BOLD);
        const int th = api->line_height(CP_FONT_UI_LARGE);
        api->draw_text(CP_FONT_UI_LARGE, x + (cell - tw) / 2, y + (cell - th) / 2, buf, filled ? 0 : 1, CP_TEXT_BOLD);
      }
    }

  if (g_won && !g_over) api->draw_text_centered(CP_FONT_UI, w / 2, gy + cell * N + 8, "达成 2048！继续挑战", 1, CP_TEXT_BOLD);
  if (g_over) {
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, gy + cell * N + 8, "游戏结束 · 确认重来", 1, CP_TEXT_BOLD);
  }
  app_hints(api, "返回", g_over ? "重来" : "", "方向键", "滑动");
  if (g_about.open) app_about_draw(api, "2048");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "2048", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
