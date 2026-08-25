// 每日诗词 — fetches a classical Chinese verse (jinrishici), keeps a rolling
// history of the last 20 for offline browsing. Ported to .eapp.

#include "app.h"

#define HIST 20
typedef struct {
  char content[96];
  char author[40];
  char origin[48];
} Poem;

static Poem g_hist[HIST];
static int g_count;   // number stored
static int g_view;    // index being shown (0 = newest)
static AppAbout g_about;

static void save(const CpApi* api) {
  api->file_write("poems.bin", g_hist, sizeof(Poem) * (g_count < HIST ? g_count : HIST));
}
static void load(const CpApi* api) {
  int n = api->file_read("poems.bin", g_hist, sizeof(g_hist));
  if (n > 0) g_count = n / (int)sizeof(Poem);
}

static int fetch(const CpApi* api) {
  if (!api->wifi_ensure(15000)) return -1;
  static char body[2048];
  const int n = api->http_get("https://v1.jinrishici.com/all.json", body, sizeof(body) - 1);
  if (n <= 0) return -2;
  body[n] = 0;
  Poem p;
  memset(&p, 0, sizeof(p));
  if (!app_json_str(body, "content", p.content, sizeof(p.content))) return -3;
  app_json_str(body, "author", p.author, sizeof(p.author));
  app_json_str(body, "origin", p.origin, sizeof(p.origin));
  // Shift history down, insert newest at 0.
  for (int i = (g_count < HIST ? g_count : HIST) - 1; i > 0; --i) g_hist[i] = g_hist[i - 1];
  g_hist[0] = p;
  if (g_count < HIST) ++g_count;
  g_view = 0;
  save(api);
  return 0;
}

static int32_t on_enter(const CpApi* api) {
  g_count = 0;
  g_view = 0;
  load(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  if (in->released & CP_BTN_CONFIRM) {
    app_message(api, "求诗中…");
    if (fetch(api) != 0) app_message(api, "获取失败，看看历史");
    api->delay_ms(600);
    return CP_LOOP_RENDER;
  }
  if ((in->released & CP_BTN_LEFT) && g_view + 1 < g_count) { ++g_view; return CP_LOOP_RENDER; }
  if ((in->released & CP_BTN_RIGHT) && g_view > 0) { --g_view; return CP_LOOP_RENDER; }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

static void on_render(const CpApi* api) {
  char buf[64];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();
  cp_snprintf(buf, sizeof(buf), "%d/%d", g_count ? g_view + 1 : 0, g_count);
  app_header(api, "每日诗词", buf);

  if (g_count == 0) {
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 10, "确认求一句", 1, CP_TEXT_BOLD);
    app_hints(api, "返回", "换一句", "左右历史", "");
    if (g_about.open) app_about_draw(api, "每日诗词");
    return;
  }

  const Poem* p = &g_hist[g_view];
  api->draw_text_wrapped(CP_FONT_TITLE, 24, 80, w - 48, 4, p->content, 1, CP_TEXT_BOLD);
  if (p->author[0] || p->origin[0]) {
    cp_snprintf(buf, sizeof(buf), "——%s《%s》", p->author, p->origin);
    api->draw_text_wrapped(CP_FONT_UI, 24, h - 120, w - 48, 2, buf, 1, CP_TEXT_REGULAR);
  }
  app_hints(api, "返回", "换一句", "左右历史", "");
  if (g_about.open) app_about_draw(api, "每日诗词");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "每日诗词", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
