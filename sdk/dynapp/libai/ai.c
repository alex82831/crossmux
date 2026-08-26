#include "ai.h"

// Three buffers, in .bss, shared by every call: the request body, the raw
// response, and the extracted answer. ~14KB, and deliberately the only large
// allocation in an AI app — there is no heap to fall back on here.
static char g_req[5120];
static char g_resp[8192];
static char g_content[3072];

// ---- tiny string helpers -------------------------------------------------

static int str_eq_n(const char* a, const char* b, int n) {
  for (int i = 0; i < n; ++i) {
    if (a[i] != b[i]) return 0;
    if (!a[i]) return 1;
  }
  return 1;
}

// Length of the UTF-8 sequence starting at `c` (1 for anything malformed, so
// a bad byte costs one step instead of running off the end).
static int u8_len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

void ai_str_copy(char* out, int cap, const char* src) {
  if (cap <= 0) return;
  if (!src) {
    out[0] = 0;
    return;
  }
  int n = 0;
  while (src[n]) {
    const int step = u8_len((unsigned char)src[n]);
    if (n + step > cap - 1) break;
    for (int i = 0; i < step; ++i) out[n + i] = src[n + i];
    n += step;
  }
  out[n] = 0;
}

int ai_str_append(char* out, int cap, const char* src) {
  int n = (int)strlen(out);
  if (!src || cap <= 0) return n;
  for (int i = 0; src[i];) {
    const int step = u8_len((unsigned char)src[i]);
    if (n + step > cap - 1) break;
    for (int k = 0; k < step; ++k) out[n + k] = src[i + k];
    n += step;
    i += step;
  }
  out[n] = 0;
  return n;
}

void ai_str_clip(char* out, int cap, const char* src, int max_bytes) {
  if (max_bytes > cap) max_bytes = cap;
  ai_str_copy(out, max_bytes, src);
}

int ai_text_normalize(char* s) {
  int r = 0, w = 0;
  // A BOM would otherwise be sent to the model and drawn as a blank glyph.
  if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) r = 3;
  for (; s[r]; ++r) {
    if (s[r] == '\r') {
      if (s[r + 1] == '\n') continue;  // CRLF -> LF
      s[w++] = '\n';
      continue;
    }
    s[w++] = s[r];
  }
  s[w] = 0;
  return w;
}

void ai_str_trim_end(char* s) {
  int n = (int)strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) --n;
  s[n] = 0;
}

// ---- JSON ----------------------------------------------------------------

typedef struct {
  char* buf;
  int cap;
  int len;
  int overflow;
} AiBuf;

static void bputc(AiBuf* b, char c) {
  if (b->len + 1 >= b->cap) {
    b->overflow = 1;
    return;
  }
  b->buf[b->len++] = c;
  b->buf[b->len] = 0;
}

static void bput(AiBuf* b, const char* s) {
  for (; s && *s; ++s) bputc(b, *s);
}

static void bput_int(AiBuf* b, int v) {
  char tmp[16];
  cp_snprintf(tmp, sizeof(tmp), "%d", v);
  bput(b, tmp);
}

// Append `s` as the inside of a JSON string. UTF-8 goes through untouched
// (legal, and half the size of \u escapes); only the mandatory escapes and
// control characters are rewritten.
static void bput_json(AiBuf* b, const char* s) {
  for (; s && *s; ++s) {
    const unsigned char c = (unsigned char)*s;
    switch (c) {
      case '"':
        bput(b, "\\\"");
        break;
      case '\\':
        bput(b, "\\\\");
        break;
      case '\n':
        bput(b, "\\n");
        break;
      case '\r':
        bput(b, "\\r");
        break;
      case '\t':
        bput(b, "\\t");
        break;
      default:
        if (c < 0x20) {
          char esc[8];
          cp_snprintf(esc, sizeof(esc), "\\u%04x", c);
          bput(b, esc);
        } else {
          bputc(b, (char)c);
        }
        break;
    }
  }
}

static int hex4(const char* p) {
  int v = 0;
  for (int i = 0; i < 4; ++i) {
    const char c = p[i];
    int d;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'a' && c <= 'f')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      d = c - 'A' + 10;
    else
      return -1;
    v = v * 16 + d;
  }
  return v;
}

static int put_utf8(char* out, int cap, int n, unsigned cp) {
  if (cp < 0x80) {
    if (n + 1 > cap - 1) return n;
    out[n++] = (char)cp;
  } else if (cp < 0x800) {
    if (n + 2 > cap - 1) return n;
    out[n++] = (char)(0xC0 | (cp >> 6));
    out[n++] = (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    if (n + 3 > cap - 1) return n;
    out[n++] = (char)(0xE0 | (cp >> 12));
    out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = (char)(0x80 | (cp & 0x3F));
  } else {
    if (n + 4 > cap - 1) return n;
    out[n++] = (char)(0xF0 | (cp >> 18));
    out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = (char)(0x80 | (cp & 0x3F));
  }
  return n;
}

// Copy a JSON string body (p points just past the opening quote) into `out`,
// undoing escapes. libapp's app_json_str() drops the backslash and keeps the
// letter, which turns every newline in an answer into a literal 'n' — fine
// for a weather code, useless for prose.
static void json_unescape(const char* p, char* out, int cap) {
  int n = 0;
  while (*p && *p != '"' && n < cap - 1) {
    if (*p != '\\') {
      out[n++] = *p++;
      continue;
    }
    ++p;
    switch (*p) {
      case 'n':
        out[n++] = '\n';
        ++p;
        break;
      case 't':
        out[n++] = '\t';
        ++p;
        break;
      case 'r':
        ++p;
        break;  // CR alone adds nothing on this display
      case 'b':
      case 'f':
        ++p;
        break;
      case 'u': {
        const int hi = hex4(p + 1);
        if (hi < 0) {
          ++p;
          break;
        }
        p += 5;
        unsigned cp = (unsigned)hi;
        // Non-BMP characters arrive as a surrogate pair.
        if (hi >= 0xD800 && hi <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
          const int lo = hex4(p + 2);
          if (lo >= 0xDC00 && lo <= 0xDFFF) {
            cp = 0x10000u + (((unsigned)hi - 0xD800u) << 10) + ((unsigned)lo - 0xDC00u);
            p += 6;
          }
        }
        n = put_utf8(out, cap, n, cp);
        break;
      }
      case 0:
        break;
      default:
        out[n++] = *p++;
        break;
    }
  }
  out[n] = 0;
}

// Find `"key"` used as an object key and return the start of its string
// value (just past the opening quote), or NULL.
static const char* json_str_at(const char* buf, const char* key) {
  char pat[40];
  cp_snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = app_find(buf, pat);
  if (!p) return 0;
  while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
  if (*p != ':') return 0;
  ++p;
  while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
  if (*p != '"') return 0;
  return p + 1;
}

// ---- configuration -------------------------------------------------------

// DeepSeek by default: OpenAI-compatible, cheap, and reachable from where
// this device is used. Any compatible endpoint works — that is the point.
static const char* kDefUrl = "https://api.deepseek.com/v1";
static const char* kDefModel = "deepseek-chat";
static const char* kDefLang = "中文";

static void load_key(const CpApi* api, const char* key, char* out, int cap, const char* fallback) {
  out[0] = 0;
  if (api->shared_get(key, out, (uint32_t)cap) <= 0 || out[0] == 0) ai_str_copy(out, cap, fallback);
}

void ai_config_load(const CpApi* api, AiConfig* cfg) {
  load_key(api, "ai_url", cfg->base_url, AI_URL_LEN, kDefUrl);
  load_key(api, "ai_key", cfg->api_key, AI_KEY_LEN, "");
  load_key(api, "ai_model", cfg->model, AI_MODEL_LEN, kDefModel);
  load_key(api, "ai_lang", cfg->lang, AI_LANG_LEN, kDefLang);
}

int ai_config_save(const CpApi* api, const AiConfig* cfg) {
  int ok = api->shared_set("ai_url", cfg->base_url);
  ok &= api->shared_set("ai_key", cfg->api_key);
  ok &= api->shared_set("ai_model", cfg->model);
  ok &= api->shared_set("ai_lang", cfg->lang);
  return ok;
}

int ai_config_ready(const AiConfig* cfg) { return cfg->base_url[0] != 0 && cfg->api_key[0] != 0 && cfg->model[0] != 0; }

void ai_endpoint(const AiConfig* cfg, char* out, int cap) {
  int n = (int)strlen(cfg->base_url);
  while (n > 0 && cfg->base_url[n - 1] == '/') --n;
  if (n > cap - 1) n = cap - 1;
  memcpy(out, cfg->base_url, (size_t)n);
  out[n] = 0;
  // Tolerate a base_url that already carries the full path.
  const int tail = (int)strlen("/chat/completions");
  if (n >= tail && str_eq_n(out + n - tail, "/chat/completions", tail)) return;
  ai_str_append(out, cap, "/chat/completions");
}

// ---- the call ------------------------------------------------------------

static const char* http_error_text(int code) {
  switch (code) {
    case CP_HTTP_ERR_NO_WIFI:
      return "网络未连接，请先连上 Wi-Fi";
    case CP_HTTP_ERR_OVERFLOW:
      return "回答太长，装不下。请让它答得短一些";
    case CP_HTTP_ERR_ARGS:
      return "服务地址无效，请到设置里检查";
    default:
      return "连接服务失败：地址不对，或网络不通";
  }
}

// Turn a non-2xx body into something worth reading. Providers put the reason
// in {"error":{"message":"..."}}, so surface that verbatim rather than a
// status number the user can do nothing with.
static const char* status_error_text(void) {
  const char* p = json_str_at(g_resp, "message");
  if (!p) return "服务返回了错误，且没有说明原因";
  static char msg[320];
  ai_str_copy(msg, sizeof(msg) - 32, "服务返回错误：");
  char detail[240];
  json_unescape(p, detail, sizeof(detail));
  ai_str_append(msg, sizeof(msg), detail);
  return msg;
}

static int build_request(const AiConfig* cfg, const char* sys, const AiTurn* turns, int count, int max_tokens) {
  AiBuf b = {g_req, (int)sizeof(g_req), 0, 0};
  g_req[0] = 0;
  bput(&b, "{\"model\":\"");
  bput_json(&b, cfg->model);
  bput(&b, "\",\"max_tokens\":");
  bput_int(&b, max_tokens > 0 ? max_tokens : 700);
  bput(&b, ",\"temperature\":0.6,\"stream\":false,\"messages\":[");
  if (sys && sys[0]) {
    bput(&b, "{\"role\":\"system\",\"content\":\"");
    bput_json(&b, sys);
    bput(&b, "\"}");
    if (count > 0) bputc(&b, ',');
  }
  for (int i = 0; i < count; ++i) {
    bput(&b, "{\"role\":\"");
    bput_json(&b, turns[i].role ? turns[i].role : "user");
    bput(&b, "\",\"content\":\"");
    bput_json(&b, turns[i].text ? turns[i].text : "");
    bput(&b, "\"}");
    if (i + 1 < count) bputc(&b, ',');
  }
  bput(&b, "]}");
  return !b.overflow;
}

// Blocking single call. Fills g_content and returns 1, or points *err at a
// message and returns 0.
static int call_now(const CpApi* api, const AiConfig* cfg, const char* sys, const AiTurn* turns, int count,
                    int max_tokens, const char** err) {
  if (!ai_config_ready(cfg)) {
    *err = "还没有配置 AI 服务。请先打开「AI 助手」填入服务地址和密钥";
    return 0;
  }
  if (!api->wifi_ensure(20000)) {
    *err = "连不上 Wi-Fi。请检查网络后重试";
    return 0;
  }
  if (!build_request(cfg, sys, turns, count, max_tokens)) {
    *err = "这次要问的内容太长了，请缩短后重试";
    return 0;
  }

  char url[AI_URL_LEN + 24];
  ai_endpoint(cfg, url, (int)sizeof(url));
  char auth[AI_KEY_LEN + 32];
  cp_snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->api_key);

  const int n = api->http_post(url, "application/json", auth, g_req, g_resp, (uint32_t)sizeof(g_resp));
  if (n < 0) {
    *err = n == CP_HTTP_ERR_STATUS ? status_error_text() : http_error_text(n);
    return 0;
  }
  g_resp[n < (int)sizeof(g_resp) ? n : (int)sizeof(g_resp) - 1] = 0;

  // Scope the search to the choices array. "content" carries its own opening
  // quote in the pattern, so a provider that also sends "reasoning_content"
  // cannot be mistaken for the answer.
  const char* scope = app_find(g_resp, "\"choices\"");
  const char* p = json_str_at(scope ? scope : g_resp, "content");
  if (!p) {
    *err = status_error_text();
    return 0;
  }
  json_unescape(p, g_content, (int)sizeof(g_content));
  if (g_content[0] == 0) {
    *err = "服务返回了空回答，请重试";
    return 0;
  }
  return 1;
}

void ai_job_start(AiJob* job, const char* note) {
  job->state = AI_ARMED;
  job->note = note;
  job->result = 0;
  job->took_ms = 0;
  job->swallow = 0;
}

int ai_job_pump(const CpApi* api, AiJob* job, const AiConfig* cfg, const char* sys, const AiTurn* turns, int count,
                int max_tokens) {
  if (job->state != AI_ARMED) return 0;
  job->started_ms = api->millis();
  const char* err = 0;
  const int ok = call_now(api, cfg, sys, turns, count, max_tokens, &err);
  job->took_ms = api->millis() - job->started_ms;
  job->state = ok ? AI_DONE : AI_FAILED;
  job->result = ok ? g_content : err;
  job->swallow = 1;
  return 1;
}

int ai_job_settled(AiJob* job) {
  if (!job->swallow) return 0;
  job->swallow = 0;
  return 1;
}

void ai_job_draw_busy(const CpApi* api, const AiJob* job, const char* title) {
  api->clear_screen();
  const int y = app_header(api, title, 0);
  const int w = api->screen_width(), h = api->screen_height();
  const int cy = y + (h - y - APP_FOOTER_H) / 2;
  api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, cy - api->line_height(CP_FONT_UI_LARGE), "正在思考…", 1,
                          CP_TEXT_BOLD);
  if (job->note && job->note[0]) {
    api->draw_text_centered(CP_FONT_UI, w / 2, cy + 6, job->note, 1, CP_TEXT_REGULAR);
  }
  api->draw_text_centered(CP_FONT_SMALL, w / 2, cy + 6 + api->line_height(CP_FONT_UI) + 10, "通常需要 10-40 秒，请稍候",
                          1, CP_TEXT_REGULAR);
  app_hints(api, "请稍候", 0, 0, 0);
}

char* ai_scratch(int* cap) {
  if (cap) *cap = (int)sizeof(g_req);
  g_req[0] = 0;
  return g_req;
}

// ---- text entry ----------------------------------------------------------

int ai_input_begin(const CpApi* api, AiInput* inp, const char* title, const char* initial, int max_len) {
  if (max_len <= 0 || max_len > AI_INPUT_MAX - 1) max_len = AI_INPUT_MAX - 1;
  inp->buf[0] = 0;
  inp->pending = api->text_input_begin(title, initial ? initial : "", (uint32_t)max_len) ? 1 : 0;
  return inp->pending;
}

int ai_input_poll(const CpApi* api, AiInput* inp) {
  if (!inp->pending) return -2;
  const int r = api->text_input_result(inp->buf, (uint32_t)sizeof(inp->buf));
  if (r == 0) return 0;
  inp->pending = 0;
  return r > 0 ? 1 : -1;
}

// ---- pager ---------------------------------------------------------------

void ai_pager_set(const CpApi* api, AiPager* p, const char* text, int font, int x, int y, int w, int h) {
  p->text = text ? text : "";
  p->font = font;
  p->style = CP_TEXT_REGULAR;
  p->x = x;
  p->y = y;
  p->w = w;
  p->line_h = api->line_height(font);
  if (p->line_h < 1) p->line_h = 16;
  p->lines_per_page = h / p->line_h;
  if (p->lines_per_page < 1) p->lines_per_page = 1;
  p->page = 0;
  p->line_count = 0;

  // Per-glyph advances, summed. These are bitmap fonts, so the sum is exact
  // and a whole line never has to be re-measured.
  short ascii_w[96];
  for (int i = 0; i < 96; ++i) ascii_w[i] = -1;

  const char* s = p->text;
  int i = 0, line_start = 0, width = 0, last_space = -1;
  p->starts[0] = 0;
  char glyph[8];

  // starts[] holds 16-bit offsets, so cap the walk rather than wrap around.
  const int limit = 65000;

  while (s[i] && i < limit && p->line_count < AI_PAGER_MAX_LINES) {
    if (s[i] == '\n') {
      p->starts[++p->line_count] = (unsigned short)(i + 1);
      ++i;
      line_start = i;
      width = 0;
      last_space = -1;
      continue;
    }
    const int step = u8_len((unsigned char)s[i]);
    int gw;
    if (step == 1 && (unsigned char)s[i] >= 0x20 && (unsigned char)s[i] < 0x80) {
      const int idx = (unsigned char)s[i] - 0x20;
      if (ascii_w[idx] < 0) {
        glyph[0] = s[i];
        glyph[1] = 0;
        ascii_w[idx] = (short)api->text_width(p->font, glyph, p->style);
      }
      gw = ascii_w[idx];
    } else {
      for (int k = 0; k < step; ++k) glyph[k] = s[i + k];
      glyph[step] = 0;
      gw = api->text_width(p->font, glyph, p->style);
    }

    if (width + gw > p->w && i > line_start) {
      // Latin wraps after the last space; CJK has none, so it breaks here.
      int next = i;
      if (last_space > line_start) {
        next = last_space + 1;
        while (s[next] == ' ') ++next;  // leading run of spaces belongs to the break
      }
      p->starts[++p->line_count] = (unsigned short)next;
      line_start = next;
      i = next;
      width = 0;
      last_space = -1;
      continue;
    }

    if (s[i] == ' ') last_space = i;
    width += gw;
    i += step;
  }
  // starts[] holds MAX+1 entries: one per line, plus the end of the last one.
  // Without this guard a text that saturates the line cap would write one
  // element past the array.
  if (p->line_count < AI_PAGER_MAX_LINES && (i > line_start || p->line_count == 0)) ++p->line_count;
  p->starts[p->line_count] = (unsigned short)i;
}

int ai_pager_pages(const AiPager* p) {
  const int n = (p->line_count + p->lines_per_page - 1) / p->lines_per_page;
  return n < 1 ? 1 : n;
}

int ai_pager_scrollable(const AiPager* p) { return p->line_count > p->lines_per_page; }

void ai_pager_draw(const CpApi* api, const AiPager* p) {
  const int first = p->page * p->lines_per_page;
  char line[256];
  for (int r = 0; r < p->lines_per_page; ++r) {
    const int idx = first + r;
    if (idx >= p->line_count) break;
    int a = p->starts[idx], b = p->starts[idx + 1];
    // Drop the newline / wrapping space the break consumed.
    while (b > a && (p->text[b - 1] == '\n' || p->text[b - 1] == '\r')) --b;
    int n = b - a;
    if (n > (int)sizeof(line) - 1) {
      n = (int)sizeof(line) - 1;
      while (n > 0 && ((unsigned char)p->text[a + n] & 0xC0) == 0x80) --n;  // no split glyph
    }
    if (n < 0) n = 0;
    memcpy(line, p->text + a, (size_t)n);
    line[n] = 0;
    if (line[0]) api->draw_text(p->font, p->x, p->y + r * p->line_h, line, 1, p->style);
  }
}

int ai_pager_input(const CpApi* api, const CpInput* in, AiPager* p) {
  (void)api;
  const int pages = ai_pager_pages(p);
  const int was = p->page;
  const uint32_t fwd = CP_BTN_DOWN | CP_BTN_RIGHT | CP_BTN_PAGE_FORWARD;
  const uint32_t back = CP_BTN_UP | CP_BTN_LEFT | CP_BTN_PAGE_BACK;
  if ((in->released & fwd) && p->page + 1 < pages) ++p->page;
  if ((in->released & back) && p->page > 0) --p->page;
  return p->page != was;
}
