// 电子木鱼 — tap the wooden fish, "功德+1" floats up, cumulative count
// persists. Confirm or tap = strike. Ported to .eapp.

#include "app.h"

static uint32_t g_merit;
static uint32_t g_session;
static uint32_t g_last_strike_ms;  // for the floating "+1" animation window
static AppAbout g_about;

static void save(const CpApi* api) { api->file_write("merit.bin", &g_merit, sizeof(g_merit)); }

static void strike(const CpApi* api) {
  ++g_merit;
  ++g_session;
  g_last_strike_ms = api->millis();
  if ((g_session % 10) == 0) save(api);
}

static int32_t on_enter(const CpApi* api) {
  api->file_read("merit.bin", &g_merit, sizeof(g_merit));
  g_session = 0;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  if ((in->released & CP_BTN_CONFIRM) || in->tapped) {
    strike(api);
    return CP_LOOP_RENDER;
  }
  // Repaint once to clear the "+1" after it fades.
  if (g_last_strike_ms && api->millis() - g_last_strike_ms > 600) {
    g_last_strike_ms = 0;
    return CP_LOOP_RENDER;
  }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

// Draw a simple wooden-fish glyph centered at (cx,cy).
static void draw_fish(const CpApi* api, int cx, int cy) {
  const int rx = 70, ry = 46;
  // body: filled rounded blob approximated by nested rectangles
  api->fill_rect(cx - rx, cy - ry + 10, 2 * rx, 2 * ry - 20, 1);
  api->fill_rect(cx - rx + 10, cy - ry, 2 * rx - 20, 2 * ry, 1);
  // hollow mouth slot (white)
  api->fill_rect(cx - 40, cy + ry - 26, 80, 16, 0);
  api->draw_line(cx - 40, cy + ry - 18, cx + 40, cy + ry - 18, 0);
}

static void on_render(const CpApi* api) {
  char buf[40];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();
  app_header(api, "电子木鱼", "");

  draw_fish(api, w / 2, h / 2);

  cp_snprintf(buf, sizeof(buf), "功德 %u", g_merit);
  api->draw_text_centered(CP_FONT_TITLE, w / 2, 70, buf, 1, CP_TEXT_BOLD);

  if (g_last_strike_ms && api->millis() - g_last_strike_ms <= 600) {
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 90, "功德 +1", 1, CP_TEXT_BOLD);
  }
  cp_snprintf(buf, sizeof(buf), "本次 %u", g_session);
  api->draw_text_centered(CP_FONT_UI, w / 2, h - 70, buf, 1, CP_TEXT_REGULAR);
  app_hints(api, "返回", "敲击", "点击=敲", "");
  if (g_about.open) app_about_draw(api, "电子木鱼");
}

static void on_exit(const CpApi* api) { save(api); }

static const CpApp kApp = {CP_ABI_VERSION, 0, "电子木鱼", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
