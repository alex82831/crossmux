// 生命游戏 — sample dynamic app.
// Conway's Game of Life on a coarse grid sized for e-ink. Confirm =
// run/pause, Up = randomize, Down = single step, tap toggles a cell while
// paused. Persists the grid to the app's sandbox on exit and restores it.

#include "crosspoint_app_abi.h"
#include "mini_libc.h"

#define CELL 8
#define MAX_COLS 100
#define MAX_ROWS 96
#define TOP_BAR 34

static uint8_t g_grid[MAX_ROWS][MAX_COLS];
static uint8_t g_next[MAX_ROWS][MAX_COLS];
static int g_cols, g_rows;
static int g_origin_x, g_origin_y;
static uint8_t g_running;
static uint32_t g_generation;
static uint32_t g_last_step_ms;

static void randomize(const CpApi* api) {
  for (int r = 0; r < g_rows; ++r) {
    for (int c = 0; c < g_cols; ++c) {
      g_grid[r][c] = (api->random_u32() % 100u) < 28u ? 1 : 0;
    }
  }
  g_generation = 0;
}

static void step(void) {
  for (int r = 0; r < g_rows; ++r) {
    for (int c = 0; c < g_cols; ++c) {
      int n = 0;
      for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
          if (dr == 0 && dc == 0) continue;
          const int rr = (r + dr + g_rows) % g_rows;  // toroidal wrap
          const int cc = (c + dc + g_cols) % g_cols;
          n += g_grid[rr][cc];
        }
      }
      g_next[r][c] = g_grid[r][c] ? (n == 2 || n == 3) : (n == 3);
    }
  }
  memcpy(g_grid, g_next, sizeof(g_grid));
  ++g_generation;
}

static int32_t on_enter(const CpApi* api) {
  const int w = api->screen_width();
  const int h = api->screen_height();
  g_cols = w / CELL;
  g_rows = (h - TOP_BAR - 30) / CELL;
  if (g_cols > MAX_COLS) g_cols = MAX_COLS;
  if (g_rows > MAX_ROWS) g_rows = MAX_ROWS;
  g_origin_x = (w - g_cols * CELL) / 2;
  g_origin_y = TOP_BAR + 4;
  g_running = 0;
  g_generation = 0;
  memset(g_grid, 0, sizeof(g_grid));
  if (api->file_read("grid.bin", g_grid, sizeof(g_grid)) <= 0) {
    randomize(api);
  }
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* input) {
  if (input->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  uint32_t flags = CP_LOOP_IDLE;
  if (input->released & CP_BTN_CONFIRM) {
    g_running = !g_running;
    flags |= CP_LOOP_RENDER;
  }
  if (input->released & CP_BTN_UP) {
    randomize(api);
    flags |= CP_LOOP_RENDER;
  }
  if ((input->released & CP_BTN_DOWN) && !g_running) {
    step();
    flags |= CP_LOOP_RENDER;
  }
  if (input->tapped && !g_running) {
    const int c = (input->touch_x - g_origin_x) / CELL;
    const int r = (input->touch_y - g_origin_y) / CELL;
    if (c >= 0 && c < g_cols && r >= 0 && r < g_rows) {
      g_grid[r][c] ^= 1;
      flags |= CP_LOOP_RENDER;
    }
  }
  if (g_running && api->millis() - g_last_step_ms >= 350u) {
    g_last_step_ms = api->millis();
    step();
    flags |= CP_LOOP_RENDER;
  }
  if (flags == CP_LOOP_IDLE) api->delay_ms(30);
  return flags;
}

static void on_render(const CpApi* api) {
  char buf[40];
  const int w = api->screen_width();
  const int h = api->screen_height();

  api->clear_screen();
  api->fill_rect(0, 0, w, TOP_BAR, 1);
  api->draw_text(CP_FONT_UI, 12, 7, "生命游戏", 0, CP_TEXT_BOLD);
  cp_snprintf(buf, sizeof(buf), "第 %u 代 %s", g_generation, g_running ? ">>" : "||");
  api->draw_text(CP_FONT_UI, w - 12 - api->text_width(CP_FONT_UI, buf, CP_TEXT_REGULAR), 7, buf, 0,
                 CP_TEXT_REGULAR);

  for (int r = 0; r < g_rows; ++r) {
    for (int c = 0; c < g_cols; ++c) {
      if (g_grid[r][c]) {
        api->fill_rect(g_origin_x + c * CELL, g_origin_y + r * CELL, CELL - 1, CELL - 1, 1);
      }
    }
  }
  api->draw_rect(g_origin_x - 2, g_origin_y - 2, g_cols * CELL + 3, g_rows * CELL + 3, 1);
  api->draw_text(CP_FONT_SMALL, 12, h - 24, "确认=运行/暂停 · 上=随机 · 下=单步 · 点击=翻转", 1,
                 CP_TEXT_REGULAR);
}

static void on_exit(const CpApi* api) { api->file_write("grid.bin", g_grid, sizeof(g_grid)); }

static const CpApp kApp = {
    CP_ABI_VERSION, 0, "生命游戏", 10000, on_enter, on_loop, on_render, on_exit,
};

__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
