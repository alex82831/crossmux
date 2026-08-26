// 计算器 — a 4x5 on-screen calculator grid navigated by the D-pad (or touch).
// Integer + decimal arithmetic, chained ops. Ported to .eapp.

#include "app.h"

static const char* kKeys[20] = {
    "C", "±", "%", "÷", "7", "8", "9", "×", "4", "5", "6", "-", "1", "2", "3", "+", "0", ".", "⌫", "=",
};

// Fixed-point: values are int64 micro-units (value * 1e6). No floating point
// (soft-float would need libgcc, which we cannot link).
#define FP_SCALE 1000000LL

static char g_display[24];
static long long g_acc;  // fixed-point accumulator
static char g_pending;   // 0, '+','-','*','/'
static int g_fresh;      // next digit starts a new number
static int g_sel;        // selected key 0..19
static AppAbout g_about;

static void set_display_num(long long v) {
  int neg = v < 0;
  if (neg) v = -v;
  long long ip = v / FP_SCALE, fp = v % FP_SCALE;
  if (fp == 0) {
    cp_snprintf(g_display, sizeof(g_display), "%s%d", neg ? "-" : "", (int)ip);
  } else {
    char frac[8];
    cp_snprintf(frac, sizeof(frac), "%06d", (int)fp);
    int end = 6;
    while (end > 1 && frac[end - 1] == '0') --end;
    frac[end] = 0;
    cp_snprintf(g_display, sizeof(g_display), "%s%d.%s", neg ? "-" : "", (int)ip, frac);
  }
}

static long long display_val(void) {
  long long sign = 1, ip = 0, fp = 0, scale = 1;
  const char* p = g_display;
  if (*p == '-') {
    sign = -1;
    ++p;
  }
  while (*p >= '0' && *p <= '9') {
    ip = ip * 10 + (*p - '0');
    ++p;
  }
  if (*p == '.') {
    ++p;
    while (*p >= '0' && *p <= '9' && scale < FP_SCALE) {
      fp = fp * 10 + (*p - '0');
      scale *= 10;
      ++p;
    }
  }
  return sign * (ip * FP_SCALE + fp * (FP_SCALE / scale));
}

static void input_digit(char d) {
  if (g_fresh) {
    g_display[0] = d;
    g_display[1] = 0;
    g_fresh = 0;
    return;
  }
  int len = (int)strlen(g_display);
  if (len < 14) {
    if (!(len == 1 && g_display[0] == '0' && d != '.')) {
      g_display[len] = d;
      g_display[len + 1] = 0;
    } else {
      g_display[0] = d;  // replace leading zero
    }
  }
}

static void input_dot(void) {
  if (g_fresh) {
    g_display[0] = '0';
    g_display[1] = '.';
    g_display[2] = 0;
    g_fresh = 0;
    return;
  }
  if (!strchr(g_display, '.')) {
    int len = (int)strlen(g_display);
    g_display[len] = '.';
    g_display[len + 1] = 0;
  }
}

static void apply_pending(void) {
  long long b = display_val();
  if (g_pending == '+')
    g_acc += b;
  else if (g_pending == '-')
    g_acc -= b;
  else if (g_pending == '*')
    g_acc = (g_acc * b) / FP_SCALE;
  else if (g_pending == '/')
    g_acc = (b != 0) ? (g_acc * FP_SCALE) / b : 0;
  else
    g_acc = b;
}

static void press(const CpApi* api, int k) {
  (void)api;
  if (k >= 4 && (k % 4) != 3) {  // digit rows, non-operator columns
    // digit keys: rows 1..4 cols 0..2, plus '0' at 16
  }
  const char* key = kKeys[k];
  if (key[0] >= '0' && key[0] <= '9' && key[1] == 0) {
    input_digit(key[0]);
    return;
  }
  if (k == 16) {
    input_digit('0');
    return;
  }  // "0"
  if (k == 17) {
    input_dot();
    return;
  }  // "."
  if (k == 0) {
    g_acc = 0;
    g_pending = 0;
    g_fresh = 1;
    set_display_num(0);
    return;
  }  // C
  if (k == 1) {
    set_display_num(-display_val());
    g_fresh = 1;
    return;
  }  // ±
  if (k == 2) {
    set_display_num(display_val() / 100);
    g_fresh = 1;
    return;
  }  // %
  if (k == 18) {  // backspace
    int len = (int)strlen(g_display);
    if (len > 1)
      g_display[len - 1] = 0;
    else {
      g_display[0] = '0';
      g_display[1] = 0;
    }
    return;
  }
  char op = 0;
  if (k == 3)
    op = '/';
  else if (k == 7)
    op = '*';
  else if (k == 11)
    op = '-';
  else if (k == 15)
    op = '+';
  if (op) {
    apply_pending();
    set_display_num(g_acc);
    g_pending = op;
    g_fresh = 1;
    return;
  }
  if (k == 19) {  // =
    apply_pending();
    set_display_num(g_acc);
    g_pending = 0;
    g_fresh = 1;
  }
}

static int32_t on_enter(const CpApi* api) {
  (void)api;
  g_display[0] = '0';
  g_display[1] = 0;
  g_acc = 0;
  g_pending = 0;
  g_fresh = 1;
  g_sel = 12;  // "1"
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  uint32_t f = CP_LOOP_IDLE;
  if (in->released & CP_BTN_LEFT) {
    g_sel = (g_sel % 4 == 0) ? g_sel + 3 : g_sel - 1;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_RIGHT) {
    g_sel = (g_sel % 4 == 3) ? g_sel - 3 : g_sel + 1;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_UP) {
    g_sel = (g_sel < 4) ? g_sel + 16 : g_sel - 4;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_DOWN) {
    g_sel = (g_sel >= 16) ? g_sel - 16 : g_sel + 4;
    f = CP_LOOP_RENDER;
  } else if (in->released & CP_BTN_CONFIRM) {
    press(api, g_sel);
    f = CP_LOOP_RENDER;
  }
  if (f == CP_LOOP_IDLE) api->delay_ms(40);
  return f;
}

static void on_render(const CpApi* api) {
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();
  const int top = app_header(api, "计算器", "");

  // Display
  const int dispH = 56;
  api->draw_rect(16, top + 6, w - 32, dispH, 1);
  const int tw = api->text_width(CP_FONT_TITLE, g_display, CP_TEXT_BOLD);
  api->draw_text(CP_FONT_TITLE, w - 26 - tw, top + 6 + (dispH - api->line_height(CP_FONT_TITLE)) / 2, g_display, 1,
                 CP_TEXT_BOLD);

  // Keypad 4x5
  const int gy = top + 6 + dispH + 10;
  const int gh = h - gy - 34;
  const int cw = (w - 32) / 4;
  const int ch = gh / 5;
  for (int i = 0; i < 20; ++i) {
    const int r = i / 4, c = i % 4;
    const int x = 16 + c * cw;
    const int y = gy + r * ch;
    const int sel = (i == g_sel);
    if (sel)
      api->fill_rect(x, y, cw - 2, ch - 2, 1);
    else
      api->draw_rect(x, y, cw - 2, ch - 2, 1);
    const int kw = api->text_width(CP_FONT_UI_LARGE, kKeys[i], CP_TEXT_BOLD);
    const int kh = api->line_height(CP_FONT_UI_LARGE);
    api->draw_text(CP_FONT_UI_LARGE, x + (cw - 2 - kw) / 2, y + (ch - 2 - kh) / 2, kKeys[i], sel ? 0 : 1, CP_TEXT_BOLD);
  }
  app_hints(api, "返回", "确认", "方向选键", "");
  if (g_about.open) app_about_draw(api, "计算器");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "计算器", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
