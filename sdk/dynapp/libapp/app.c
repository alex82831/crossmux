#include "app.h"

// Copyright block shown in every app's About overlay — preserved from the
// firmware's earlier "关于" pages.
static const char* kAuthor = "作者：Alex Xin";
static const char* kContact = "alex82831gm@gmail.com";
static const char* kLocation = "中国·西安";
static const char* kRights = "版权所有";

int app_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int app_header(const CpApi* api, const char* title, const char* right) {
  const int w = api->screen_width();
  api->fill_rect(0, 0, w, APP_HEADER_H, 1);
  api->draw_text(CP_FONT_UI, 12, 8, title, 0, CP_TEXT_BOLD);
  if (right && right[0]) {
    const int rw = api->text_width(CP_FONT_UI, right, CP_TEXT_REGULAR);
    api->draw_text(CP_FONT_UI, w - 12 - rw, 8, right, 0, CP_TEXT_REGULAR);
  }
  return APP_HEADER_H + 6;
}

void app_hints(const CpApi* api, const char* b1, const char* b2, const char* b3, const char* b4) {
  const int h = api->screen_height();
  const char* labels[4] = {b1, b2, b3, b4};
  const int y = h - 24;
  // Pack non-empty labels evenly across the width with a dot separator.
  char line[128];
  int pos = 0;
  for (int i = 0; i < 4; ++i) {
    if (!labels[i] || !labels[i][0]) continue;
    if (pos > 0 && pos < (int)sizeof(line) - 4) {
      line[pos++] = ' ';
      line[pos++] = (char)0xC2;  // "·" utf-8 lead
      line[pos++] = (char)0xB7;
      line[pos++] = ' ';
    }
    for (const char* p = labels[i]; *p && pos < (int)sizeof(line) - 1; ++p) line[pos++] = *p;
  }
  line[pos] = 0;
  api->draw_text(CP_FONT_SMALL, 12, y, line, 1, CP_TEXT_REGULAR);
}

void app_menu_draw(const CpApi* api, int top_y, const char* const* items, int count, int sel) {
  const int w = api->screen_width();
  const int rowH = 40;
  const int pad = 20;
  for (int i = 0; i < count; ++i) {
    const int y = top_y + i * (rowH + 6);
    const int selrow = (i == sel);
    if (selrow) api->fill_rect(pad, y, w - 2 * pad, rowH, 1);
    else api->draw_rect(pad, y, w - 2 * pad, rowH, 1);
    const int tw = api->text_width(CP_FONT_UI_LARGE, items[i], CP_TEXT_REGULAR);
    const int th = api->line_height(CP_FONT_UI_LARGE);
    api->draw_text(CP_FONT_UI_LARGE, (w - tw) / 2, y + (rowH - th) / 2, items[i], selrow ? 0 : 1, CP_TEXT_REGULAR);
  }
}

int app_menu_input(const CpApi* api, const CpInput* in, int* sel, int count) {
  (void)api;
  if (count <= 0) return 0;
  if ((in->released & CP_BTN_UP) || (in->released & CP_BTN_LEFT)) *sel = (*sel + count - 1) % count;
  if ((in->released & CP_BTN_DOWN) || (in->released & CP_BTN_RIGHT)) *sel = (*sel + 1) % count;
  if (in->released & CP_BTN_CONFIRM) return 1;
  return 0;
}

void app_message(const CpApi* api, const char* line) {
  api->clear_screen();
  api->draw_text_centered(CP_FONT_UI_LARGE, api->screen_width() / 2, api->screen_height() / 2 - 10, line, 1,
                          CP_TEXT_BOLD);
}

void app_about_draw(const CpApi* api, const char* app_title) {
  const int w = api->screen_width();
  const int h = api->screen_height();
  const int bw = w * 4 / 5;
  const int bh = h / 2;
  const int x = (w - bw) / 2;
  const int y = (h - bh) / 2;
  api->fill_rect(x - 2, y - 2, bw + 4, bh + 4, 1);  // shadow frame
  api->fill_rect(x, y, bw, bh, 0);                  // white panel
  api->draw_rect(x + 3, y + 3, bw - 6, bh - 6, 1);

  const int cx = w / 2;
  int ty = y + 22;
  api->draw_text_centered(CP_FONT_UI_LARGE, cx, ty, app_title, 1, CP_TEXT_BOLD);
  ty += api->line_height(CP_FONT_UI_LARGE) + 14;
  api->draw_text_centered(CP_FONT_UI, cx, ty, kAuthor, 1, CP_TEXT_REGULAR);
  ty += api->line_height(CP_FONT_UI) + 4;
  api->draw_text_centered(CP_FONT_SMALL, cx, ty, kContact, 1, CP_TEXT_REGULAR);
  ty += api->line_height(CP_FONT_SMALL) + 8;
  api->draw_text_centered(CP_FONT_UI, cx, ty, kLocation, 1, CP_TEXT_REGULAR);
  ty += api->line_height(CP_FONT_UI) + 4;
  api->draw_text_centered(CP_FONT_UI, cx, ty, kRights, 1, CP_TEXT_REGULAR);
}

int app_about_input(const CpApi* api, const CpInput* in, AppAbout* about, int allow_hold, int* repaint) {
  static uint32_t back_down;   // one app at a time, so file-scope is fine
  static int consume_release;  // swallow the release that opened the panel
  if (repaint) *repaint = 0;

  if (about->open) {
    if (consume_release) {
      if (in->released & CP_BTN_BACK) consume_release = 0;
      return 1;
    }
    if (in->released || in->tapped) {
      about->open = 0;
      if (repaint) *repaint = 1;
    }
    return 1;
  }
  if (allow_hold && (in->held & CP_BTN_BACK)) {
    if (back_down == 0) back_down = api->millis();
    if (api->millis() - back_down >= 900) {
      about->open = 1;
      consume_release = 1;
      back_down = 0;
      if (repaint) *repaint = 1;
      return 1;
    }
    return 0;  // still deciding — let the app keep the frame
  }
  back_down = 0;
  return 0;
}

// --- text/JSON scanning ---------------------------------------------------
const char* app_find(const char* buf, const char* needle) {
  if (!buf || !needle) return 0;
  const int nl = (int)strlen(needle);
  for (const char* p = buf; *p; ++p) {
    int i = 0;
    while (i < nl && p[i] == needle[i]) ++i;
    if (i == nl) return p + nl;
  }
  return 0;
}

int app_json_int1000(const char* buf, const char* key, long* out) {
  char pat[48];
  cp_snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = app_find(buf, pat);
  if (!p) return 0;
  while (*p && *p != ':') ++p;
  if (*p != ':') return 0;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;
  int neg = 0;
  if (*p == '-') { neg = 1; ++p; }
  long ip = 0;
  int any = 0;
  while (*p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); ++p; any = 1; }
  long fp = 0, scale = 1;
  if (*p == '.') {
    ++p;
    for (int d = 0; d < 3 && *p >= '0' && *p <= '9'; ++d) { fp = fp * 10 + (*p - '0'); scale *= 10; ++p; }
  }
  if (!any) return 0;
  long v = ip * 1000 + fp * (1000 / scale);
  *out = neg ? -v : v;
  return 1;
}

int app_json_str(const char* buf, const char* key, char* out, int cap) {
  char pat[48];
  cp_snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = app_find(buf, pat);
  if (!p) return 0;
  while (*p && *p != ':') ++p;
  if (*p != ':') return 0;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != '"') return 0;
  ++p;
  int n = 0;
  while (*p && *p != '"' && n < cap - 1) {
    if (*p == '\\' && p[1]) ++p;  // skip escape lead, copy next byte literally
    out[n++] = *p++;
  }
  out[n] = 0;
  return 1;
}
