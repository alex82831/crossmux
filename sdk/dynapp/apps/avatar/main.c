// 头像生成器 — procedurally draws a quirky "ugly avatar" from a random seed.
// Confirm rerolls. A fresh 1-bit implementation (the firmware version used a
// generator lib). Ported to .eapp.

#include "app.h"

static uint32_t g_seed;
static AppAbout g_about;

// Simple xorshift so a saved seed reproduces the same face.
static uint32_t g_rng;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static int rr(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

static void draw_face(const CpApi* api) {
  const int w = api->screen_width();
  const int h = api->screen_height();
  const int cx = w / 2, cy = h / 2 - 10;
  g_rng = g_seed ? g_seed : 1;

  // Head outline: wobbly ellipse.
  const int rx = rr(90, 130), ry = rr(110, 150);
  for (int a = 0; a < 360; a += 2) {
    // crude parametric point without trig: use a polygon of jitter radii
    const int jr = rr(-6, 6);
    // approximate cos/sin via lookup-free integer octants is overkill; use
    // a diamond+box blend instead for a lumpy head.
    (void)a;
    (void)jr;
    break;
  }
  // Lumpy head via nested rounded rectangles.
  api->draw_rect(cx - rx, cy - ry, 2 * rx, 2 * ry, 1);
  api->draw_rect(cx - rx + rr(2, 10), cy - ry + rr(2, 10), 2 * rx - rr(4, 20), 2 * ry - rr(4, 20), 1);

  // Eyes.
  const int eyeY = cy - rr(10, 40);
  const int eyeDX = rr(30, 55);
  for (int s = -1; s <= 1; s += 2) {
    const int ex = cx + s * eyeDX;
    const int er = rr(8, 20);
    for (int dy = -er; dy <= er; ++dy)
      for (int dx = -er; dx <= er; ++dx)
        if (dx * dx + dy * dy <= er * er && dx * dx + dy * dy >= (er - 2) * (er - 2)) api->draw_pixel(ex + dx, eyeY + dy, 1);
    // pupil
    const int px = ex + rr(-4, 4), py = eyeY + rr(-4, 4), pr = rr(2, 5);
    api->fill_rect(px - pr, py - pr, 2 * pr, 2 * pr, 1);
  }

  // Eyebrows.
  for (int s = -1; s <= 1; s += 2) {
    const int ex = cx + s * eyeDX;
    api->draw_line(ex - 18, eyeY - rr(20, 34), ex + 18, eyeY - rr(20, 34) + rr(-8, 8), 1);
  }

  // Nose.
  const int noseY = cy + rr(-6, 20);
  api->draw_line(cx, eyeY + 8, cx + rr(-14, 14), noseY, 1);
  api->draw_line(cx + rr(-14, 14), noseY, cx + rr(-8, 8), noseY + rr(4, 12), 1);

  // Mouth.
  const int mY = cy + rr(50, 90);
  const int mW = rr(30, 70);
  const int curve = rr(-16, 16);
  api->draw_line(cx - mW, mY, cx, mY + curve, 1);
  api->draw_line(cx, mY + curve, cx + mW, mY, 1);

  // Optional hair strokes.
  const int hairN = rr(0, 14);
  for (int i = 0; i < hairN; ++i) {
    const int hx = cx + rr(-rx, rx);
    api->draw_line(hx, cy - ry, hx + rr(-10, 10), cy - ry - rr(6, 26), 1);
  }
}

static int32_t on_enter(const CpApi* api) {
  g_seed = api->random_u32() | 1u;
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  if ((in->released & CP_BTN_CONFIRM) || in->tapped) {
    g_seed = api->random_u32() | 1u;
    return CP_LOOP_RENDER;
  }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

static void on_render(const CpApi* api) {
  api->clear_screen();
  app_header(api, "头像生成器", "");
  draw_face(api);
  char buf[24];
  cp_snprintf(buf, sizeof(buf), "#%u", g_seed % 100000u);
  api->draw_text_centered(CP_FONT_SMALL, api->screen_width() / 2, api->screen_height() - 56, buf, 1, CP_TEXT_REGULAR);
  app_hints(api, "返回", "换一个", "点击=换", "");
  if (g_about.open) app_about_draw(api, "头像生成器");
}

static void on_exit(const CpApi* api) { (void)api; }

static const CpApp kApp = {CP_ABI_VERSION, 0, "头像生成器", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
