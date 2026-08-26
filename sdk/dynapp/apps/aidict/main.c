// AI 词典 — look a word up once, keep it forever.
//
// The point is not the lookup; any chatbot can define a word. The point is
// that every word you look up while reading is written to the SD card as a
// fixed-format card, so the list becomes a vocabulary deck built out of your
// own reading — reviewable offline, on a device that is already in your hand
// when you hit the word.
//
// Typing cost: one word.

#include "ai.h"

#define DICT_MAX 60
#define WORD_LEN 40
#define ENTRY_LEN 2048

typedef enum { ST_LIST = 0, ST_TYPING, ST_THINKING, ST_ENTRY, ST_REVIEW } State;

static AiConfig g_cfg;
static AiJob g_job;
static AiInput g_input;
static AiPager g_pager;
static AppList g_list;
static AppAbout g_about;

static State g_state;
static char g_words[DICT_MAX][WORD_LEN];
static unsigned char g_slots[DICT_MAX];
static int g_count;
static char g_word[WORD_LEN];  // the word currently displayed
static char g_entry[ENTRY_LEN];
static char g_status[80];
static int g_confirm_delete;
static int g_review_shown;  // flashcard: is the back face up?
static int g_review_idx;

static const char* kSystem =
    "你是一部给中文读者使用的词典。对用户给出的词或短语，只按下面的固定格式输出，"
    "每项一行，不要有多余的话，也不要使用 Markdown 记号：\n"
    "释义：\n构成：（词源、词根，或逐字拆解）\n例句：（一句，附中文意思）\n"
    "辨析：（与最容易混淆的一个近义词的差别）\n记忆：（一句话的记忆办法）\n"
    "全部合计不超过 220 字。";

// --- persistence ----------------------------------------------------------
// One file per word plus a small index. Flat names because the sandbox does
// not create sub-directories, and separate files because a single blob would
// have to be held in RAM in full just to read one card.

static void slot_path(int slot, char* out, int cap) { cp_snprintf(out, (unsigned)cap, "w%02d.txt", slot); }

static void index_load(const CpApi* api) {
  int cap = 0;
  char* buf = ai_scratch(&cap);
  g_count = 0;
  const int n = api->file_read("index.txt", buf, (uint32_t)cap);
  if (n <= 0) return;
  buf[n < cap ? n : cap - 1] = 0;

  const char* p = buf;
  while (*p && g_count < DICT_MAX) {
    int slot = 0, any = 0;
    while (*p >= '0' && *p <= '9') {
      slot = slot * 10 + (*p++ - '0');
      any = 1;
    }
    if (*p == '\t') ++p;
    int w = 0;
    while (*p && *p != '\n' && w < WORD_LEN - 1) g_words[g_count][w++] = *p++;
    g_words[g_count][w] = 0;
    if (*p == '\n') ++p;
    if (any && w > 0 && slot < DICT_MAX) {
      g_slots[g_count] = (unsigned char)slot;
      ++g_count;
    }
  }
}

static void index_save(const CpApi* api) {
  int cap = 0;
  char* buf = ai_scratch(&cap);
  int n = 0;
  for (int i = 0; i < g_count; ++i) {
    char line[WORD_LEN + 8];
    cp_snprintf(line, sizeof(line), "%d\t%s\n", g_slots[i], g_words[i]);
    const int len = (int)strlen(line);
    if (n + len >= cap) break;
    memcpy(buf + n, line, (size_t)len);
    n += len;
  }
  buf[n] = 0;
  api->file_write("index.txt", buf, (uint32_t)n);
}

static int free_slot(void) {
  for (int s = 0; s < DICT_MAX; ++s) {
    int used = 0;
    for (int i = 0; i < g_count; ++i) {
      if (g_slots[i] == s) used = 1;
    }
    if (!used) return s;
  }
  return -1;
}

static int find_word(const char* word) {
  for (int i = 0; i < g_count; ++i) {
    if (strcmp(g_words[i], word) == 0) return i;
  }
  return -1;
}

static void entry_store(const CpApi* api, const char* word, const char* text) {
  int at = find_word(word);
  int slot;
  if (at >= 0) {
    slot = g_slots[at];
    // Re-looking-up a word moves it back to the top of the list.
    for (int i = at; i > 0; --i) {
      ai_str_copy(g_words[i], WORD_LEN, g_words[i - 1]);
      g_slots[i] = g_slots[i - 1];
    }
  } else {
    if (g_count == DICT_MAX) {
      char path[16];
      slot_path(g_slots[g_count - 1], path, sizeof(path));
      api->file_delete(path);  // drop the least recently used card
      --g_count;
    }
    slot = free_slot();
    if (slot < 0) slot = 0;
    for (int i = g_count; i > 0; --i) {
      ai_str_copy(g_words[i], WORD_LEN, g_words[i - 1]);
      g_slots[i] = g_slots[i - 1];
    }
    ++g_count;
  }
  ai_str_copy(g_words[0], WORD_LEN, word);
  g_slots[0] = (unsigned char)slot;

  char path[16];
  slot_path(slot, path, sizeof(path));
  api->file_write(path, text, (uint32_t)strlen(text));
  index_save(api);
}

static int entry_load(const CpApi* api, int index) {
  char path[16];
  slot_path(g_slots[index], path, sizeof(path));
  const int n = api->file_read(path, g_entry, sizeof(g_entry));
  if (n <= 0) {
    ai_str_copy(g_entry, sizeof(g_entry), "这张卡片的内容读不出来了，重新查一次即可。");
    return 0;
  }
  g_entry[n < (int)sizeof(g_entry) ? n : (int)sizeof(g_entry) - 1] = 0;
  return 1;
}

// --- flow ----------------------------------------------------------------

static void show_entry(const CpApi* api) {
  const int y = APP_HEADER_H + 6;
  ai_pager_set(api, &g_pager, g_entry, CP_FONT_UI, 16, y + 28, api->screen_width() - 32,
               api->screen_height() - y - 28 - APP_FOOTER_H);
  g_state = ST_ENTRY;
}

static int32_t on_enter(const CpApi* api) {
  ai_config_load(api, &g_cfg);
  index_load(api);
  app_list_fit(api, &g_list, APP_HEADER_H + 10, 38);
  g_state = ST_LIST;
  if (!ai_config_ready(&g_cfg)) ai_str_copy(g_status, sizeof(g_status), "请先在「AI 助手」里配置服务和密钥");
  return 0;
}

static void run_pending_call(const CpApi* api) {
  char ask[WORD_LEN + 8];
  cp_snprintf(ask, sizeof(ask), "%s", g_word);
  const AiTurn turn = {"user", ask};
  ai_job_pump(api, &g_job, &g_cfg, kSystem, &turn, 1, 500);
  if (g_job.state == AI_DONE) {
    ai_str_copy(g_entry, sizeof(g_entry), g_job.result);
    entry_store(api, g_word, g_entry);
  } else {
    ai_str_copy(g_entry, sizeof(g_entry), g_job.result ? g_job.result : "查询失败");
  }
  show_entry(api);
}

static uint32_t app_loop(const CpApi* api, const CpInput* in) {
  if (ai_job_settled(&g_job)) return CP_LOOP_IDLE;

  int repaint = 0;
  if (app_about_input(api, in, &g_about, g_state == ST_LIST, &repaint)) {
    return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  }

  switch (g_state) {
    case ST_TYPING: {
      const int r = ai_input_poll(api, &g_input);
      if (r == 0) return CP_LOOP_IDLE;
      if (r == 1 && g_input.buf[0]) {
        ai_str_copy(g_word, sizeof(g_word), g_input.buf);
        const int cached = find_word(g_word);
        if (cached >= 0) {
          // Already on the card: no call, no cost, works with Wi-Fi off.
          entry_load(api, cached);
          ai_str_copy(g_status, sizeof(g_status), "这个词已经查过了");
          show_entry(api);
        } else {
          ai_job_start(&g_job, g_word);
          g_state = ST_THINKING;
        }
      } else {
        g_state = ST_LIST;
      }
      return CP_LOOP_RENDER;
    }

    case ST_THINKING:
      run_pending_call(api);
      return CP_LOOP_RENDER;

    case ST_ENTRY:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_LIST;
        g_status[0] = 0;
        return CP_LOOP_RENDER;
      }
      if ((in->released & CP_BTN_CONFIRM) && g_job.state == AI_FAILED) {
        ai_job_start(&g_job, g_word);
        g_state = ST_THINKING;
        return CP_LOOP_RENDER;
      }
      return ai_pager_input(api, in, &g_pager) ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_REVIEW:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_LIST;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_CONFIRM) {
        if (!g_review_shown) {
          entry_load(api, g_review_idx);
          ai_str_copy(g_word, sizeof(g_word), g_words[g_review_idx]);
          const int y = APP_HEADER_H + 6;
          ai_pager_set(api, &g_pager, g_entry, CP_FONT_UI, 16, y + 28, api->screen_width() - 32,
                       api->screen_height() - y - 28 - APP_FOOTER_H);
          g_review_shown = 1;
        } else {
          g_review_idx = (int)(api->random_u32() % (uint32_t)g_count);
          g_review_shown = 0;
        }
        return CP_LOOP_RENDER;
      }
      if (g_review_shown && ai_pager_input(api, in, &g_pager)) return CP_LOOP_RENDER;
      return CP_LOOP_IDLE;

    case ST_LIST:
    default:
      if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
      // Right deletes the highlighted card; a second press confirms.
      if (in->released & CP_BTN_RIGHT) {
        if (g_list.sel > 0 && g_list.sel <= g_count) {
          const int idx = g_list.sel - 1;
          if (g_confirm_delete == g_list.sel) {
            char path[16];
            slot_path(g_slots[idx], path, sizeof(path));
            api->file_delete(path);
            for (int i = idx; i + 1 < g_count; ++i) {
              ai_str_copy(g_words[i], WORD_LEN, g_words[i + 1]);
              g_slots[i] = g_slots[i + 1];
            }
            --g_count;
            index_save(api);
            g_confirm_delete = 0;
            ai_str_copy(g_status, sizeof(g_status), "已删除");
          } else {
            g_confirm_delete = g_list.sel;
            ai_str_copy(g_status, sizeof(g_status), "再按一次「右」确认删除");
          }
        }
        return CP_LOOP_RENDER;
      }
      if ((in->released & CP_BTN_LEFT) && g_count > 0) {
        g_review_idx = (int)(api->random_u32() % (uint32_t)g_count);
        g_review_shown = 0;
        g_state = ST_REVIEW;
        return CP_LOOP_RENDER;
      }
      if (in->released) {
        g_confirm_delete = 0;
        g_status[0] = 0;
      }
      if (app_list_input(api, in, &g_list, g_count + 1)) {
        if (g_list.sel == 0) {
          if (ai_input_begin(api, &g_input, "查词（一个词或短语）", "", WORD_LEN - 1)) {
            g_state = ST_TYPING;
          } else {
            ai_str_copy(g_status, sizeof(g_status), "键盘暂时打不开，请稍后再试");
          }
        } else {
          entry_load(api, g_list.sel - 1);
          ai_str_copy(g_word, sizeof(g_word), g_words[g_list.sel - 1]);
          show_entry(api);
        }
      }
      return in->released ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  }
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  const uint32_t flags = app_loop(api, in);
  // The host keyboard is about to cover the screen, so painting our own frame
  // first would spend a full e-ink refresh on something nobody ever sees.
  if (g_state == ST_TYPING) return flags & CP_LOOP_EXIT;
  return flags;
}

// --- render --------------------------------------------------------------

static const char* list_row(int index, void* ctx) {
  (void)ctx;
  return index == 0 ? "查词…" : g_words[index - 1];
}

static void render_list(const CpApi* api) {
  char right[24];
  cp_snprintf(right, sizeof(right), "%d 个词", g_count);
  app_header(api, "AI 词典", right);
  app_list_draw_fn(api, &g_list, list_row, 0, g_count + 1);
  if (g_status[0]) {
    app_hints(api, g_status, 0, 0, 0);
  } else if (g_count > 0) {
    app_hints(api, "确认打开", "左=复习", "右=删除", "长按返回=关于");
  } else {
    app_hints(api, "查过的词会存在本机，离线也能复习", 0, 0, 0);
  }
}

static void render_entry(const CpApi* api) {
  char right[32];
  if (ai_pager_scrollable(&g_pager)) {
    cp_snprintf(right, sizeof(right), "%d/%d", g_pager.page + 1, ai_pager_pages(&g_pager));
  } else {
    right[0] = 0;
  }
  const int y = app_header(api, g_word, right);
  api->draw_line(16, y + 20, api->screen_width() - 16, y + 20, 1);
  ai_pager_draw(api, &g_pager);
  if (g_job.state == AI_FAILED) {
    app_hints(api, "确认重试", "返回", 0, 0);
  } else {
    app_hints(api, "上下翻页", "返回", 0, 0);
  }
}

static void render_review(const CpApi* api) {
  char right[24];
  cp_snprintf(right, sizeof(right), "%d 个词", g_count);
  const int y = app_header(api, "复习", right);
  const int w = api->screen_width();
  if (!g_review_shown) {
    api->draw_text_centered(CP_FONT_TITLE, w / 2, (api->screen_height() - y) / 2, g_words[g_review_idx], 1,
                            CP_TEXT_BOLD);
    app_hints(api, "确认看解释", "返回", 0, 0);
    return;
  }
  api->draw_text(CP_FONT_UI_LARGE, 16, y, g_word, 1, CP_TEXT_BOLD);
  api->draw_line(16, y + 24, w - 16, y + 24, 1);
  ai_pager_draw(api, &g_pager);
  app_hints(api, "确认换一个", "上下翻页", "返回", 0);
}

static void on_render(const CpApi* api) {
  api->clear_screen();
  switch (g_state) {
    case ST_THINKING:
      ai_job_draw_busy(api, &g_job, "AI 词典");
      break;
    case ST_ENTRY:
      render_entry(api);
      break;
    case ST_REVIEW:
      render_review(api);
      break;
    case ST_TYPING:
      app_message(api, "请输入要查的词…");
      break;
    default:
      render_list(api);
      break;
  }
  if (g_about.open) app_about_draw(api, "AI 词典");
}

static const CpApp kApp = {
    CP_ABI_VERSION, sizeof(CpApi), "AI 词典", 10000, on_enter, on_loop, on_render, 0,
};

const CpApp* cp_app_entry(void);
const CpApp* cp_app_entry(void) { return &kApp; }
