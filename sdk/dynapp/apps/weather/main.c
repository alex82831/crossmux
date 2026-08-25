// 天气 — Open-Meteo current conditions + 5-day forecast for 40 Chinese
// cities. Wi-Fi on demand, last result cached to SD. Ported to .eapp.

#include "app.h"
#include "cities.h"

static int g_city;
static int g_haveData;
static long g_curTemp1000;
static int g_curCode;
static long g_maxT[5], g_minT[5];
static int g_code[5], g_pop[5];
static int g_days;
static AppAbout g_about;

static const char* code_desc(int c) {
  if (c == 0) return "晴";
  if (c <= 2) return "少云";
  if (c == 3) return "多云";
  if (c <= 48) return "雾";
  if (c <= 57) return "毛毛雨";
  if (c <= 67) return "雨";
  if (c <= 77) return "雪";
  if (c <= 82) return "阵雨";
  if (c <= 86) return "阵雪";
  return "雷雨";
}

// Read up to `max` comma-separated numbers (x1000) from an array after key.
static int read_array(const char* buf, const char* key, long* out, int max) {
  char pat[48];
  cp_snprintf(pat, sizeof(pat), "\"%s\":[", key);
  const char* p = app_find(buf, pat);
  if (!p) return 0;
  int n = 0;
  while (*p && *p != ']' && n < max) {
    while (*p == ',' || *p == ' ') ++p;
    if (*p == ']') break;
    int neg = 0;
    if (*p == '-') {
      neg = 1;
      ++p;
    }
    long ip = 0, fp = 0, scale = 1;
    int any = 0;
    while (*p >= '0' && *p <= '9') {
      ip = ip * 10 + (*p - '0');
      ++p;
      any = 1;
    }
    if (*p == '.') {
      ++p;
      for (int d = 0; d < 3 && *p >= '0' && *p <= '9'; ++d) {
        fp = fp * 10 + (*p - '0');
        scale *= 10;
        ++p;
      }
    }
    if (!any) break;
    long v = ip * 1000 + fp * (1000 / scale);
    out[n++] = neg ? -v : v;
    while (*p && *p != ',' && *p != ']') ++p;
  }
  return n;
}

static void save_cache(const CpApi* api) {
  // Simple binary cache of parsed values.
  struct {
    int city, code, days, cur;
    long temp;
    long mx[5], mn[5];
    int cd[5], pp[5];
  } c;
  memset(&c, 0, sizeof(c));
  c.city = g_city;
  c.code = g_curCode;
  c.days = g_days;
  c.cur = 1;
  c.temp = g_curTemp1000;
  for (int i = 0; i < 5; ++i) {
    c.mx[i] = g_maxT[i];
    c.mn[i] = g_minT[i];
    c.cd[i] = g_code[i];
    c.pp[i] = g_pop[i];
  }
  api->file_write("wx.bin", &c, sizeof(c));
}
static void load_cache(const CpApi* api) {
  struct {
    int city, code, days, cur;
    long temp;
    long mx[5], mn[5];
    int cd[5], pp[5];
  } c;
  if (api->file_read("wx.bin", &c, sizeof(c)) != (int)sizeof(c)) return;
  g_city = c.city;
  g_curCode = c.code;
  g_days = c.days;
  g_curTemp1000 = c.temp;
  g_haveData = 1;
  for (int i = 0; i < 5; ++i) {
    g_maxT[i] = c.mx[i];
    g_minT[i] = c.mn[i];
    g_code[i] = c.cd[i];
    g_pop[i] = c.pp[i];
  }
}

static int fetch(const CpApi* api) {
  if (!api->wifi_ensure(15000)) return -1;
  static char url[256];
  const int lat = kCities[g_city].lat100, lon = kCities[g_city].lon100;
  cp_snprintf(url, sizeof(url),
              "https://api.open-meteo.com/v1/forecast?latitude=%d.%02d&longitude=%d.%02d"
              "&current=temperature_2m,weather_code"
              "&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max"
              "&timezone=Asia%%2FShanghai&forecast_days=5",
              lat / 100, lat % 100, lon / 100, lon % 100);
  static char body[4096];
  const int n = api->http_get(url, body, sizeof(body) - 1);
  if (n <= 0) return -2;
  body[n] = 0;

  // current block
  const char* cur = app_find(body, "\"current\":");
  long t;
  if (cur && app_json_int1000(cur, "temperature_2m", &t)) g_curTemp1000 = t;
  long code;
  if (cur && app_json_int1000(cur, "weather_code", &code)) g_curCode = (int)(code / 1000);

  long tmp[5];
  g_days = read_array(body, "temperature_2m_max", g_maxT, 5);
  read_array(body, "temperature_2m_min", g_minT, 5);
  int nc = read_array(body, "weather_code", tmp, 5);
  for (int i = 0; i < nc; ++i) g_code[i] = (int)(tmp[i] / 1000);
  int np = read_array(body, "precipitation_probability_max", tmp, 5);
  for (int i = 0; i < np; ++i) g_pop[i] = (int)(tmp[i] / 1000);
  g_haveData = (g_days > 0);
  if (g_haveData) save_cache(api);
  return g_haveData ? 0 : -3;
}

static int32_t on_enter(const CpApi* api) {
  g_city = 0;
  load_cache(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  if (in->released & CP_BTN_UP) {
    g_city = (g_city + WCITY_COUNT - 1) % WCITY_COUNT;
    return CP_LOOP_RENDER;
  }
  if (in->released & CP_BTN_DOWN) {
    g_city = (g_city + 1) % WCITY_COUNT;
    return CP_LOOP_RENDER;
  }
  if (in->released & CP_BTN_CONFIRM) {
    app_message(api, "联网获取中…");
    const int r = fetch(api);
    if (r != 0) app_message(api, "获取失败，保留旧数据");
    api->delay_ms(r != 0 ? 800 : 0);
    return CP_LOOP_RENDER;
  }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

static void on_render(const CpApi* api) {
  char buf[48];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();
  app_header(api, "天气", kCities[g_city].name);

  if (!g_haveData) {
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 20, "确认联网获取天气", 1, CP_TEXT_BOLD);
    api->draw_text_centered(CP_FONT_UI, w / 2, h / 2 + 16, "上下切换城市", 1, CP_TEXT_REGULAR);
    app_hints(api, "返回", "刷新", "上下城市", "");
    if (g_about.open) app_about_draw(api, "天气");
    return;
  }

  cp_snprintf(buf, sizeof(buf), "%d°C  %s", (int)(g_curTemp1000 / 1000), code_desc(g_curCode));
  api->draw_text_centered(CP_FONT_TITLE, w / 2, 64, buf, 1, CP_TEXT_BOLD);

  int y = 128;
  const char* wk[] = {"今天", "明天", "后天", "第4天", "第5天"};
  for (int i = 0; i < g_days && i < 5; ++i) {
    cp_snprintf(buf, sizeof(buf), "%s  %d~%d°C  %s  降水%d%%", wk[i], (int)(g_minT[i] / 1000), (int)(g_maxT[i] / 1000),
                code_desc(g_code[i]), g_pop[i]);
    api->draw_text(CP_FONT_UI, 20, y, buf, 1, CP_TEXT_REGULAR);
    y += api->line_height(CP_FONT_UI) + 10;
  }
  app_hints(api, "返回", "刷新", "上下城市", "");
  if (g_about.open) app_about_draw(api, "天气");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "天气", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
