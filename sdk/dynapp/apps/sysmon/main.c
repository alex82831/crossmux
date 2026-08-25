// 系统监视器 — sample dynamic app.
// Terminal-style live dashboard: battery, free heap with history graph,
// uptime, screen info. Refreshes every 2 seconds; Confirm forces a refresh.

#include "crosspoint_app_abi.h"
#include "mini_libc.h"

#define HISTORY_LEN 60

static uint32_t g_last_sample_ms;
static uint32_t g_heap_history[HISTORY_LEN];  // KB
static int g_history_count;
static uint32_t g_start_ms;

static void sample(const CpApi* api) {
  const uint32_t kb = api->free_heap() / 1024u;
  if (g_history_count < HISTORY_LEN) {
    g_heap_history[g_history_count++] = kb;
  } else {
    memmove(g_heap_history, g_heap_history + 1, sizeof(g_heap_history[0]) * (HISTORY_LEN - 1));
    g_heap_history[HISTORY_LEN - 1] = kb;
  }
}

static int32_t on_enter(const CpApi* api) {
  g_start_ms = api->millis();
  g_last_sample_ms = 0;
  g_history_count = 0;
  api->log("sysmon", "enter");
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* input) {
  if (input->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  const uint32_t now = api->millis();
  if ((input->released & CP_BTN_CONFIRM) || now - g_last_sample_ms >= 2000u) {
    g_last_sample_ms = now;
    sample(api);
    return CP_LOOP_RENDER;
  }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

static void draw_kv(const CpApi* api, const int x, int* y, const char* key, const char* value) {
  api->draw_text(CP_FONT_UI, x, *y, key, 1, CP_TEXT_REGULAR);
  api->draw_text(CP_FONT_UI, x + 150, *y, value, 1, CP_TEXT_BOLD);
  *y += api->line_height(CP_FONT_UI) + 6;
}

static void on_render(const CpApi* api) {
  char buf[48];
  const int w = api->screen_width();
  const int h = api->screen_height();

  api->clear_screen();

  // Terminal-style header bar.
  api->fill_rect(0, 0, w, 34, 1);
  api->draw_text(CP_FONT_UI, 12, 7, "SYS://MONITOR", 0, CP_TEXT_BOLD);
  cp_snprintf(buf, sizeof(buf), "%02u:%02u", (api->millis() - g_start_ms) / 60000u,
              ((api->millis() - g_start_ms) / 1000u) % 60u);
  api->draw_text(CP_FONT_UI, w - 12 - api->text_width(CP_FONT_UI, buf, CP_TEXT_BOLD), 7, buf, 0, CP_TEXT_BOLD);

  int y = 52;
  const int x = 16;

  cp_snprintf(buf, sizeof(buf), "%d%%", api->battery_percent());
  draw_kv(api, x, &y, "电量", buf);

  const uint32_t heap = api->free_heap();
  cp_snprintf(buf, sizeof(buf), "%u.%u KB", heap / 1024u, (heap % 1024u) / 103u);
  draw_kv(api, x, &y, "空闲内存", buf);

  const uint32_t up = (api->millis() - g_start_ms) / 1000u;
  cp_snprintf(buf, sizeof(buf), "%u:%02u:%02u", up / 3600u, (up / 60u) % 60u, up % 60u);
  draw_kv(api, x, &y, "运行时间", buf);

  cp_snprintf(buf, sizeof(buf), "%d x %d", w, h);
  draw_kv(api, x, &y, "屏幕", buf);

  // Heap history graph, oscilloscope style.
  const int gx = 16, gw = w - 32, gh = 140;
  const int gy = y + 12;
  api->draw_rect(gx, gy, gw, gh, 1);
  for (int i = 1; i < 4; ++i) {  // grid lines
    const int ly = gy + gh * i / 4;
    for (int px = gx + 2; px < gx + gw - 2; px += 6) api->draw_pixel(px, ly, 1);
  }
  if (g_history_count > 1) {
    uint32_t maxKb = 1;
    for (int i = 0; i < g_history_count; ++i) {
      if (g_heap_history[i] > maxKb) maxKb = g_heap_history[i];
    }
    maxKb = (maxKb + 31u) / 32u * 32u;  // headroom, rounded to 32KB
    int prevX = 0, prevY = 0;
    for (int i = 0; i < g_history_count; ++i) {
      const int px = gx + 2 + (gw - 4) * i / (HISTORY_LEN - 1);
      const int py = gy + gh - 2 - (int)((gh - 4) * g_heap_history[i] / maxKb);
      if (i > 0) api->draw_line(prevX, prevY, px, py, 1);
      prevX = px;
      prevY = py;
    }
    cp_snprintf(buf, sizeof(buf), "%u KB", maxKb);
    api->draw_text(CP_FONT_SMALL, gx + 4, gy + 3, buf, 1, CP_TEXT_REGULAR);
  }
  api->draw_text(CP_FONT_SMALL, gx, gy + gh + 6, "空闲内存走势 · 2s/样本", 1, CP_TEXT_REGULAR);

  api->draw_text(CP_FONT_SMALL, x, h - 24, "确认=刷新 · 返回=退出", 1, CP_TEXT_REGULAR);
}

static void on_exit(const CpApi* api) { api->log("sysmon", "exit"); }

static const CpApp kApp = {
    CP_ABI_VERSION, 0, "系统监视器", 10000, on_enter, on_loop, on_render, on_exit,
};

__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
