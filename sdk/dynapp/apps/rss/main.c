// RSS 速览 — fetch a few built-in feeds over Wi-Fi, extract article titles by
// scanning <item>…<title>. Titles cached to SD for offline reading. Feed URLs
// live in an editable /apps/data/rss/feeds.txt (one per line: name|url).
// Ported to .eapp.

#include "app.h"

#define MAXFEEDS 6
#define MAXITEMS 12
#define TITLELEN 96

typedef struct {
  char name[32];
  char url[160];
} Feed;

static Feed g_feeds[MAXFEEDS];
static int g_feedCount;
static char g_titles[MAXITEMS][TITLELEN];
static int g_itemCount;
static int g_curFeed;
static int g_curItem;
static int g_view;  // 0=feed list, 1=article list
static AppAbout g_about;

// Built-in defaults (also written to feeds.txt on first run so users can edit).
static const char* kDefaults =
    "少数派|https://sspai.com/feed\n"
    "阮一峰|https://www.ruanyifeng.com/blog/atom.xml\n"
    "36氪|https://36kr.com/feed\n"
    "Solidot|https://www.solidot.org/index.rss\n"
    "BBC中文|https://feeds.bbci.co.uk/zhongwen/simp/rss.xml\n";

static void parse_feed_line(const char* line, int len, Feed* f) {
  int i = 0, n = 0;
  for (; i < len && line[i] != '|' && n < (int)sizeof(f->name) - 1; ++i) f->name[n++] = line[i];
  f->name[n] = 0;
  while (i < len && line[i] != '|') ++i;
  if (i < len) ++i;  // skip '|'
  n = 0;
  for (; i < len && line[i] != '\n' && line[i] != '\r' && n < (int)sizeof(f->url) - 1; ++i) f->url[n++] = line[i];
  f->url[n] = 0;
}

static void load_feeds(const CpApi* api) {
  static char buf[1024];
  int n = api->file_read("feeds.txt", buf, sizeof(buf) - 1);
  if (n <= 0) {
    // First run: seed the editable file with defaults.
    api->file_write("feeds.txt", kDefaults, (uint32_t)strlen(kDefaults));
    cp_snprintf(buf, sizeof(buf), "%s", kDefaults);
    n = (int)strlen(buf);
  }
  buf[n] = 0;
  g_feedCount = 0;
  int start = 0;
  for (int i = 0; i <= n && g_feedCount < MAXFEEDS; ++i) {
    if (buf[i] == '\n' || buf[i] == 0) {
      if (i > start + 2) {
        parse_feed_line(buf + start, i - start, &g_feeds[g_feedCount]);
        if (g_feeds[g_feedCount].url[0]) ++g_feedCount;
      }
      start = i + 1;
    }
  }
}

// Minimal HTML-entity + CDATA cleanup while copying a title.
static void copy_title(const char* src, int len, char* out, int cap) {
  int n = 0;
  for (int i = 0; i < len && n < cap - 1; ++i) {
    if (src[i] == '<') break;  // stop at a nested tag
    out[n++] = src[i];
  }
  out[n] = 0;
}

static int fetch_feed(const CpApi* api, int idx) {
  if (!api->wifi_ensure(15000)) return -1;
  static char body[8192];
  const int n = api->http_get(g_feeds[idx].url, body, sizeof(body) - 1);
  if (n <= 0) return -2;
  body[n] = 0;
  g_itemCount = 0;
  const char* p = body;
  // Skip the channel-level title: jump to first <item> or <entry>.
  const char* firstItem = app_find(body, "<item");
  if (!firstItem) firstItem = app_find(body, "<entry");
  if (firstItem) p = firstItem;
  while (g_itemCount < MAXITEMS) {
    const char* t = app_find(p, "<title");
    if (!t) break;
    while (*t && *t != '>') ++t;  // skip attributes
    if (*t == '>') ++t;
    if (t[0] == '<' && t[1] == '!') {  // CDATA
      const char* c = app_find(t, "[CDATA[");
      if (c) t = c;
    }
    // find end
    const char* e = t;
    while (*e && *e != '<') ++e;
    copy_title(t, (int)(e - t), g_titles[g_itemCount], TITLELEN);
    if (g_titles[g_itemCount][0]) ++g_itemCount;
    p = e;
  }
  // cache titles for this feed
  char path[32];
  cp_snprintf(path, sizeof(path), "cache%d.bin", idx);
  api->file_write(path, g_titles, sizeof(g_titles[0]) * g_itemCount);
  return g_itemCount > 0 ? 0 : -3;
}

static void load_cache(const CpApi* api, int idx) {
  char path[32];
  cp_snprintf(path, sizeof(path), "cache%d.bin", idx);
  int n = api->file_read(path, g_titles, sizeof(g_titles));
  g_itemCount = n > 0 ? n / TITLELEN : 0;
}

static int32_t on_enter(const CpApi* api) {
  g_view = 0;
  g_curFeed = 0;
  load_feeds(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (g_view == 0) {
    if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
    if (g_feedCount > 0 && (in->released & CP_BTN_UP)) { g_curFeed = (g_curFeed + g_feedCount - 1) % g_feedCount; return CP_LOOP_RENDER; }
    if (g_feedCount > 0 && (in->released & CP_BTN_DOWN)) { g_curFeed = (g_curFeed + 1) % g_feedCount; return CP_LOOP_RENDER; }
    if (in->released & CP_BTN_CONFIRM) {
      app_message(api, "抓取中…");
      if (fetch_feed(api, g_curFeed) != 0) { load_cache(api, g_curFeed); }
      g_curItem = 0;
      g_view = 1;
      return CP_LOOP_RENDER;
    }
    if ((in->released & CP_BTN_LEFT) || (in->released & CP_BTN_RIGHT)) {  // fetch all
      for (int i = 0; i < g_feedCount; ++i) {
        app_message(api, g_feeds[i].name);
        fetch_feed(api, i);
      }
      return CP_LOOP_RENDER;
    }
    api->delay_ms(50);
    return CP_LOOP_IDLE;
  }

  // article list
  if (in->released & CP_BTN_BACK) { g_view = 0; return CP_LOOP_RENDER; }
  if (g_itemCount > 0 && (in->released & CP_BTN_UP)) { g_curItem = (g_curItem + g_itemCount - 1) % g_itemCount; return CP_LOOP_RENDER; }
  if (g_itemCount > 0 && (in->released & CP_BTN_DOWN)) { g_curItem = (g_curItem + 1) % g_itemCount; return CP_LOOP_RENDER; }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

static void on_render(const CpApi* api) {
  char buf[48];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == 0) {
    app_header(api, "RSS 速览", "订阅源");
    if (g_feedCount == 0) {
      api->draw_text_centered(CP_FONT_UI, w / 2, h / 2, "无订阅源，请编辑 feeds.txt", 1, CP_TEXT_REGULAR);
    } else {
      int y = 56;
      for (int i = 0; i < g_feedCount; ++i) {
        const int sel = (i == g_curFeed);
        if (sel) api->fill_rect(14, y - 4, w - 28, 34, 1);
        api->draw_text(CP_FONT_UI_LARGE, 22, y, g_feeds[i].name, sel ? 0 : 1, CP_TEXT_REGULAR);
        y += 40;
      }
    }
    app_hints(api, "返回", "打开", "上下选择", "左右全抓");
    if (g_about.open) app_about_draw(api, "RSS 速览");
    return;
  }

  cp_snprintf(buf, sizeof(buf), "%d/%d", g_itemCount ? g_curItem + 1 : 0, g_itemCount);
  app_header(api, g_feeds[g_curFeed].name, buf);
  if (g_itemCount == 0) {
    api->draw_text_centered(CP_FONT_UI, w / 2, h / 2, "暂无内容（抓取失败或无缓存）", 1, CP_TEXT_REGULAR);
  } else {
    // Show a window of titles around the cursor.
    int first = g_curItem - 3;
    if (first < 0) first = 0;
    int y = 54;
    for (int i = first; i < g_itemCount && y < h - 60; ++i) {
      const int sel = (i == g_curItem);
      if (sel) api->fill_rect(10, y - 2, w - 20, 44, 1);
      api->draw_text_wrapped(CP_FONT_UI, 16, y, w - 32, 2, g_titles[i], sel ? 0 : 1, CP_TEXT_REGULAR);
      y += 48;
    }
  }
  app_hints(api, "返回", "", "上下翻条目", "");
  if (g_about.open) app_about_draw(api, "RSS 速览");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "RSS 速览", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
