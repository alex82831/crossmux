// 单词卡 — CET-4 flashcards with a three-box Leitner flow. Confirm reveals
// the gloss, Up = knew it, Down = didn't. Per-word progress persists.
// Ported to .eapp.

#include "app.h"
#include "words.h"

static unsigned char g_box[VOCAB_COUNT];  // 0=new 1=learning 2=mastered
static int g_cur, g_prev = -1;
static int g_revealed;
static unsigned short g_seen;
static int g_dirty;
static AppAbout g_about;

static void save(const CpApi* api) {
  api->file_write("vocab.bin", g_box, sizeof(g_box));
  g_dirty = 0;
}
static void load(const CpApi* api) {
  int n = api->file_read("vocab.bin", g_box, sizeof(g_box));
  if (n != (int)sizeof(g_box)) memset(g_box, 0, sizeof(g_box));
  for (int i = 0; i < VOCAB_COUNT; ++i)
    if (g_box[i] > 2) g_box[i] = 0;
}

static void pick_next(const CpApi* api) {
  unsigned int total = 0;
  for (int i = 0; i < VOCAB_COUNT; ++i) total += g_box[i] == 0 ? 6 : g_box[i] == 1 ? 3 : 1;
  for (int attempt = 0; attempt < 4; ++attempt) {
    unsigned int target = api->random_u32() % total;
    for (int i = 0; i < VOCAB_COUNT; ++i) {
      const unsigned int wgt = g_box[i] == 0 ? 6 : g_box[i] == 1 ? 3 : 1;
      if (target < wgt) {
        if (i == g_prev && attempt < 3) break;
        g_cur = i;
        g_prev = i;
        g_revealed = 0;
        return;
      }
      target -= wgt;
    }
  }
  g_cur = (int)(api->random_u32() % VOCAB_COUNT);
  g_prev = g_cur;
  g_revealed = 0;
}

static void grade(const CpApi* api, int knew) {
  if (!g_revealed) return;
  if (knew) {
    if (g_box[g_cur] < 2) ++g_box[g_cur];
  } else {
    g_box[g_cur] = 0;
  }
  g_dirty = 1;
  if ((++g_seen % 10) == 0) save(api);
  pick_next(api);
}

static int32_t on_enter(const CpApi* api) {
  load(api);
  pick_next(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  if (in->released & CP_BTN_CONFIRM) {
    if (!g_revealed)
      g_revealed = 1;
    else
      pick_next(api);
    return CP_LOOP_RENDER;
  }
  if ((in->released & CP_BTN_UP) || (in->released & CP_BTN_LEFT)) {
    grade(api, 1);
    return CP_LOOP_RENDER;
  }
  if ((in->released & CP_BTN_DOWN) || (in->released & CP_BTN_RIGHT)) {
    grade(api, 0);
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

  int fresh = 0, learning = 0, mastered = 0;
  for (int i = 0; i < VOCAB_COUNT; ++i) {
    if (g_box[i] == 0)
      ++fresh;
    else if (g_box[i] == 1)
      ++learning;
    else
      ++mastered;
  }
  cp_snprintf(buf, sizeof(buf), "%d/%d", mastered, VOCAB_COUNT);
  app_header(api, "单词卡", buf);

  const VWord* wd = &kWords[g_cur];
  api->draw_text_centered(CP_FONT_TITLE, w / 2, 90, wd->word, 1, CP_TEXT_BOLD);
  api->draw_text_centered(CP_FONT_UI, w / 2, 140, wd->pos, 1, CP_TEXT_REGULAR);

  const char* boxTxt = g_box[g_cur] == 0 ? "新词" : g_box[g_cur] == 1 ? "学习中" : "已掌握";
  api->draw_text_centered(CP_FONT_SMALL, w / 2, 172, boxTxt, 1, CP_TEXT_REGULAR);

  if (g_revealed) {
    api->draw_text_wrapped(CP_FONT_UI_LARGE, 20, 210, w - 40, 3, wd->meaning, 1, CP_TEXT_BOLD);
  } else {
    api->draw_text_centered(CP_FONT_UI, w / 2, 220, "确认显示释义", 1, CP_TEXT_REGULAR);
  }

  cp_snprintf(buf, sizeof(buf), "新 %d · 学 %d · 掌 %d", fresh, learning, mastered);
  api->draw_text_centered(CP_FONT_UI, w / 2, h - 66, buf, 1, CP_TEXT_REGULAR);

  app_hints(api, "返回", g_revealed ? "下一个" : "显示", "上=认识", "下=不认识");
  if (g_about.open) app_about_draw(api, "单词卡");
}

static void on_exit(const CpApi* api) {
  if (g_dirty) save(api);
}

static const CpApp kApp = {CP_ABI_VERSION, 0, "单词卡", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
