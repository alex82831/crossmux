// 汇率 — foreign currency vs CNY (open.er-api.com, base CNY). Shows how many
// CNY one unit of each currency is worth, for 1/10/100/1000 units. Offline
// cache. Ported to .eapp.

#include "app.h"

typedef struct {
  const char* code;
  const char* name;
} Cur;
static const Cur kCur[] = {
    {"USD", "美元"}, {"EUR", "欧元"}, {"JPY", "日元"},   {"GBP", "英镑"},
    {"HKD", "港币"}, {"KRW", "韩元"}, {"TWD", "新台币"}, {"AUD", "澳元"},
};
#define NCUR (int)(sizeof(kCur) / sizeof(kCur[0]))
static const int kAmt[4] = {1, 10, 100, 1000};

// CNY-per-unit x1000 for each currency (i.e. 1 USD = g_cny/1000 CNY).
static long g_cny[NCUR];
static int g_haveData;
static int g_amtIdx;
static AppAbout g_about;

static void save(const CpApi* api) { api->file_write("fx.bin", g_cny, sizeof(g_cny)); }
static void load(const CpApi* api) {
  if (api->file_read("fx.bin", g_cny, sizeof(g_cny)) == (int)sizeof(g_cny)) {
    for (int i = 0; i < NCUR; ++i)
      if (g_cny[i] > 0) g_haveData = 1;
  }
}

static int fetch(const CpApi* api) {
  if (!api->wifi_ensure(15000)) return -1;
  static char body[4096];
  const int n = api->http_get("https://open.er-api.com/v6/latest/CNY", body, sizeof(body) - 1);
  if (n <= 0) return -2;
  body[n] = 0;
  const char* rates = app_find(body, "\"rates\":");
  if (!rates) return -3;
  int ok = 0;
  for (int i = 0; i < NCUR; ++i) {
    long r1000;  // CNY -> currency rate x1000 (e.g. USD 0.140 -> 140)
    if (app_json_int1000(rates, kCur[i].code, &r1000) && r1000 > 0) {
      // CNY per one unit = 1 / (rate) → (1000*1000)/r1000, keep x1000.
      g_cny[i] = 1000000L / r1000;
      ok = 1;
    } else {
      g_cny[i] = 0;
    }
  }
  g_haveData = ok;
  if (ok) save(api);
  return ok ? 0 : -4;
}

static int32_t on_enter(const CpApi* api) {
  g_amtIdx = 0;
  load(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  if (in->released & CP_BTN_UP) {
    g_amtIdx = (g_amtIdx + 3) % 4;
    return CP_LOOP_RENDER;
  }
  if (in->released & CP_BTN_DOWN) {
    g_amtIdx = (g_amtIdx + 1) % 4;
    return CP_LOOP_RENDER;
  }
  if (in->released & CP_BTN_CONFIRM) {
    app_message(api, "联网获取汇率…");
    if (fetch(api) != 0) app_message(api, "获取失败，保留旧数据");
    api->delay_ms(600);
    return CP_LOOP_RENDER;
  }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

static void on_render(const CpApi* api) {
  char buf[64];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();
  cp_snprintf(buf, sizeof(buf), "%d 单位", kAmt[g_amtIdx]);
  app_header(api, "汇率", buf);

  if (!g_haveData) {
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 10, "确认联网获取汇率", 1, CP_TEXT_BOLD);
    app_hints(api, "返回", "刷新", "上下切换金额", "");
    if (g_about.open) app_about_draw(api, "汇率");
    return;
  }

  int y = 64;
  const int amt = kAmt[g_amtIdx];
  for (int i = 0; i < NCUR; ++i) {
    if (g_cny[i] <= 0) continue;
    const long totalCny = g_cny[i] * amt;  // x1000
    cp_snprintf(buf, sizeof(buf), "%d %s（%s） = %d.%02d 元", amt, kCur[i].name, kCur[i].code, (int)(totalCny / 1000),
                (int)((totalCny % 1000) / 10));
    api->draw_text(CP_FONT_UI_LARGE, 22, y, buf, 1, CP_TEXT_REGULAR);
    y += api->line_height(CP_FONT_UI_LARGE) + 8;
  }
  api->draw_text(CP_FONT_SMALL, 22, h - 46, "数据来源 open.er-api.com", 1, CP_TEXT_REGULAR);
  app_hints(api, "返回", "刷新", "上下金额", "");
  if (g_about.open) app_about_draw(api, "汇率");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "汇率", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
