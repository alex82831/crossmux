// 抽卡伙伴 — a light "crack open a mystery egg, reveal a buddy card" novelty.
// Confirm cracks; a random buddy with a name + rarity is revealed and the
// collection count persists. Fresh implementation of the firmware idea.
// Ported to .eapp.

#include "app.h"

static const char* kNames[] = {"团子", "阿墨", "小雪", "旺财", "皮蛋", "糯米", "豆豆", "咪咪",
                               "石榴", "山楂", "布丁", "可乐", "拿铁", "芋圆", "汤圆", "麻薯"};
#define NNAMES (int)(sizeof(kNames) / sizeof(kNames[0]))
static const char* kRarity[] = {"普通", "稀有", "史诗", "传说"};

enum { S_EGG, S_CRACK, S_CARD };

static int g_stage;
static int g_name, g_rarity;
static uint32_t g_crack_ms;
static uint32_t g_collected;
static AppAbout g_about;

static void reveal(const CpApi* api) {
  g_name = (int)(api->random_u32() % NNAMES);
  const uint32_t r = api->random_u32() % 100u;
  g_rarity = r < 55 ? 0 : r < 82 ? 1 : r < 96 ? 2 : 3;  // weighted
  ++g_collected;
  api->file_write("buddy.bin", &g_collected, sizeof(g_collected));
}

static int32_t on_enter(const CpApi* api) {
  api->file_read("buddy.bin", &g_collected, sizeof(g_collected));
  g_stage = S_EGG;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;

  if (g_stage == S_CRACK) {
    if (api->millis() - g_crack_ms > 700) {
      reveal(api);
      g_stage = S_CARD;
      return CP_LOOP_RENDER;
    }
    api->delay_ms(60);
    return CP_LOOP_RENDER;  // animate the crack
  }
  if ((in->released & CP_BTN_CONFIRM) || in->tapped) {
    g_stage = S_CRACK;
    g_crack_ms = api->millis();
    return CP_LOOP_RENDER;
  }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

static void draw_egg(const CpApi* api, int cx, int cy, int cracked) {
  const int rx = 60, ry = 78;
  api->draw_rect(cx - rx, cy - ry + 12, 2 * rx, 2 * ry - 12, 1);
  api->draw_rect(cx - rx + 8, cy - ry, 2 * rx - 16, 24, 1);
  if (cracked) {
    api->draw_line(cx - rx, cy, cx - 10, cy - 14, 1);
    api->draw_line(cx - 10, cy - 14, cx + 10, cy + 10, 1);
    api->draw_line(cx + 10, cy + 10, cx + rx, cy - 6, 1);
  }
}

static void on_render(const CpApi* api) {
  char buf[40];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();
  cp_snprintf(buf, sizeof(buf), "已收集 %u", g_collected);
  app_header(api, "抽卡伙伴", buf);

  const int cx = w / 2, cy = h / 2 - 20;
  if (g_stage == S_EGG) {
    draw_egg(api, cx, cy, 0);
    api->draw_text_centered(CP_FONT_UI, cx, h - 80, "确认或点击开蛋", 1, CP_TEXT_REGULAR);
  } else if (g_stage == S_CRACK) {
    draw_egg(api, cx, cy, 1);
    api->draw_text_centered(CP_FONT_UI_LARGE, cx, h - 80, "咔——", 1, CP_TEXT_BOLD);
  } else {
    // Card
    const int cw = 180, ch = 220;
    api->draw_rect(cx - cw / 2, cy - ch / 2, cw, ch, 1);
    api->draw_rect(cx - cw / 2 + 4, cy - ch / 2 + 4, cw - 8, ch - 8, 1);
    api->draw_text_centered(CP_FONT_TITLE, cx, cy - 60, kNames[g_name], 1, CP_TEXT_BOLD);
    // little face
    api->draw_rect(cx - 26, cy - 16, 52, 44, 1);
    api->fill_rect(cx - 14, cy - 4, 6, 6, 1);
    api->fill_rect(cx + 8, cy - 4, 6, 6, 1);
    api->draw_line(cx - 12, cy + 16, cx + 12, cy + 16, 1);
    cp_snprintf(buf, sizeof(buf), "★ %s", kRarity[g_rarity]);
    api->draw_text_centered(CP_FONT_UI, cx, cy + 66, buf, 1, CP_TEXT_BOLD);
    api->draw_text_centered(CP_FONT_UI, cx, h - 80, "确认再抽一张", 1, CP_TEXT_REGULAR);
  }
  app_hints(api, "返回", g_stage == S_CARD ? "再抽" : "开蛋", "", "");
  if (g_about.open) app_about_draw(api, "抽卡伙伴");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "抽卡伙伴", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
