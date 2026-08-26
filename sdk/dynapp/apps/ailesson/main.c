// 每日一课 — one knowledge card a day, and not a single keystroke.
//
// Two things separate this from "ask a chatbot for a fact":
//
//   * It is a curriculum, not a shuffle. The titles of everything you have
//     already been taught go back to the model as context, so it moves
//     forward through a subject instead of circling the same famous three.
//
//   * The card is written to the card and read from there. Today's lesson
//     costs one call, ever; re-opening it — on the bus, with Wi-Fi off —
//     costs nothing. Past lessons stay readable offline too.
//
// Every card ends with one concrete thing to try today, which is what makes
// it a lesson rather than trivia.

#include "ai.h"

#define SEEN_MAX 24
#define BODY_SLOTS 12
#define TITLE_LEN 48
#define BODY_LEN 2048
#define SUBJECT_LEN 64

typedef enum { ST_TODAY = 0, ST_PICK, ST_TYPING, ST_THINKING, ST_ARCHIVE } State;

typedef struct {
  long date;  // yyyymmdd, 0 when the clock was unset
  unsigned char slot;
  char title[TITLE_LEN];
} Seen;

static AiConfig g_cfg;
static AiJob g_job;
static AiInput g_input;
static AiPager g_pager;
static AppList g_list;
static AppAbout g_about;

static State g_state;
static Seen g_seen[SEEN_MAX];
static int g_seen_count;
static char g_subject[SUBJECT_LEN];
static char g_body[BODY_LEN];
static char g_status[80];
static char g_sys[512];
static char g_ask[1536];
static long g_today;
static int g_have_today;
static int g_next_slot;
static int g_pick_sel;
static int g_viewing;  // index into g_seen of the card on screen, -1 = none

static const char* kSubjects[] = {
    "一个科学原理",     "一个经济学概念", "一个心理学效应", "一个哲学观点",           "一个中文成语",
    "一句地道英文表达", "一个编程知识点", "一个健康常识",   "一个历史事件的来龙去脉", "自定义…",
};
#define SUBJECT_COUNT ((int)(sizeof(kSubjects) / sizeof(kSubjects[0])))

// --- persistence ----------------------------------------------------------

static void body_path(int slot, char* out, int cap) { cp_snprintf(out, (unsigned)cap, "L%02d.txt", slot); }

static void seen_load(const CpApi* api) {
  int cap = 0;
  char* buf = ai_scratch(&cap);
  g_seen_count = 0;
  const int n = api->file_read("seen.txt", buf, (uint32_t)cap);
  if (n <= 0) return;
  buf[n < cap ? n : cap - 1] = 0;

  const char* p = buf;
  while (*p && g_seen_count < SEEN_MAX) {
    long date = 0;
    while (*p >= '0' && *p <= '9') date = date * 10 + (*p++ - '0');
    if (*p == '\t') ++p;
    int slot = 0;
    while (*p >= '0' && *p <= '9') slot = slot * 10 + (*p++ - '0');
    if (*p == '\t') ++p;
    int t = 0;
    while (*p && *p != '\n' && t < TITLE_LEN - 1) g_seen[g_seen_count].title[t++] = *p++;
    g_seen[g_seen_count].title[t] = 0;
    if (*p == '\n') ++p;
    if (t == 0) continue;
    g_seen[g_seen_count].date = date;
    g_seen[g_seen_count].slot = (unsigned char)(slot % BODY_SLOTS);
    ++g_seen_count;
  }
}

static void seen_save(const CpApi* api) {
  int cap = 0;
  char* buf = ai_scratch(&cap);
  int n = 0;
  for (int i = 0; i < g_seen_count; ++i) {
    char line[TITLE_LEN + 24];
    cp_snprintf(line, sizeof(line), "%d\t%d\t%s\n", (int)g_seen[i].date, g_seen[i].slot, g_seen[i].title);
    const int len = (int)strlen(line);
    if (n + len >= cap) break;
    memcpy(buf + n, line, (size_t)len);
    n += len;
  }
  buf[n] = 0;
  api->file_write("seen.txt", buf, (uint32_t)n);
}

// --- lesson helpers -------------------------------------------------------

// The model is asked to start with "标题：…"; that line is the index entry.
static void extract_title(const char* body, char* out, int cap) {
  const char* p = app_find(body, "标题：");
  if (!p) p = body;
  while (*p == ' ') ++p;
  int n = 0;
  while (*p && *p != '\n' && n < cap - 1) out[n++] = *p++;
  out[n] = 0;
  if (out[0] == 0) ai_str_copy(out, cap, "今日一课");
}

static void build_prompts(void) {
  ai_str_copy(
      g_sys, sizeof(g_sys),
      "你在给一台电子墨水屏阅读器写每日学习卡片。严格按下面的格式输出，"
      "每项一行，不要 Markdown 记号，不要多余的话：\n"
      "标题：（不超过 14 个字）\n是什么：\n为什么重要：\n一个例子：\n今天可以试试：（一个五分钟内能做完的具体动作）\n"
      "全部合计不超过 260 字。");
  ai_str_copy(g_ask, sizeof(g_ask), "今天请教我");
  ai_str_append(g_ask, sizeof(g_ask), g_subject[0] ? g_subject : "一个科学原理");
  ai_str_append(g_ask, sizeof(g_ask), "。");
  if (g_seen_count > 0) {
    ai_str_append(g_ask, sizeof(g_ask), "我已经学过下面这些，请不要重复，并且尽量在它们的基础上往前走一步：\n");
    for (int i = 0; i < g_seen_count; ++i) {
      ai_str_append(g_ask, sizeof(g_ask), g_seen[i].title);
      ai_str_append(g_ask, sizeof(g_ask), "\n");
    }
  }
}

static void layout_body(const CpApi* api) {
  const int y = APP_HEADER_H + 6;
  ai_pager_set(api, &g_pager, g_body, CP_FONT_UI, 16, y + 8, api->screen_width() - 32,
               api->screen_height() - y - 8 - APP_FOOTER_H);
}

static int load_lesson(const CpApi* api, int index) {
  char path[16];
  body_path(g_seen[index].slot, path, sizeof(path));
  const int n = api->file_read(path, g_body, sizeof(g_body));
  if (n <= 0) {
    ai_str_copy(g_body, sizeof(g_body), "这一课的内容已经不在卡上了（存档只保留最近 12 课）。");
    layout_body(api);
    return 0;
  }
  g_body[n < (int)sizeof(g_body) ? n : (int)sizeof(g_body) - 1] = 0;
  layout_body(api);
  return 1;
}

static void store_lesson(const CpApi* api) {
  char title[TITLE_LEN];
  extract_title(g_body, title, sizeof(title));

  // Replace today's entry if there is one, so "换一课" does not pile up.
  int at = -1;
  if (g_today != 0 && g_seen_count > 0 && g_seen[0].date == g_today) at = 0;
  if (at < 0) {
    if (g_seen_count == SEEN_MAX) --g_seen_count;
    for (int i = g_seen_count; i > 0; --i) g_seen[i] = g_seen[i - 1];
    ++g_seen_count;
    at = 0;
    g_seen[0].slot = (unsigned char)g_next_slot;
    g_next_slot = (g_next_slot + 1) % BODY_SLOTS;
  }
  g_seen[at].date = g_today;
  ai_str_copy(g_seen[at].title, TITLE_LEN, title);

  char path[16];
  body_path(g_seen[at].slot, path, sizeof(path));
  api->file_write(path, g_body, (uint32_t)strlen(g_body));
  seen_save(api);
  g_have_today = 1;
  g_viewing = at;
}

static void start_lesson(const CpApi* api) {
  (void)api;
  build_prompts();
  ai_job_start(&g_job, g_subject[0] ? g_subject : "今天的一课");
  g_state = ST_THINKING;
}

static void run_pending_call(const CpApi* api) {
  const AiTurn turn = {"user", g_ask};
  ai_job_pump(api, &g_job, &g_cfg, g_sys, &turn, 1, 600);
  ai_str_copy(g_body, sizeof(g_body), g_job.result ? g_job.result : "生成失败");
  if (g_job.state == AI_DONE) store_lesson(api);
  layout_body(api);
  g_state = ST_TODAY;
}

// --- lifecycle -----------------------------------------------------------

static int32_t on_enter(const CpApi* api) {
  ai_config_load(api, &g_cfg);
  const int sn = api->file_read("subject.txt", g_subject, sizeof(g_subject) - 1);
  g_subject[sn > 0 && sn < (int)sizeof(g_subject) ? sn : 0] = 0;
  ai_str_trim_end(g_subject);
  seen_load(api);
  g_viewing = -1;

  int32_t y = 0, mo = 0, d = 0;
  g_today = api->rtc_now(&y, &mo, &d, 0, 0, 0, 0) ? (long)y * 10000 + mo * 100 + d : 0;
  g_next_slot = g_seen_count > 0 ? (g_seen[0].slot + 1) % BODY_SLOTS : 0;

  if (g_seen_count > 0 && g_today != 0 && g_seen[0].date == g_today) {
    g_have_today = load_lesson(api, 0);
    g_viewing = 0;
  }
  app_list_fit(api, &g_list, APP_HEADER_H + 10, 38);
  g_state = g_subject[0] ? ST_TODAY : ST_PICK;
  if (!ai_config_ready(&g_cfg)) ai_str_copy(g_status, sizeof(g_status), "请先在「AI 助手」里配置服务和密钥");
  return 0;
}

static void pick_activate(const CpApi* api, int sel) {
  if (sel == SUBJECT_COUNT - 1) {
    if (ai_input_begin(api, &g_input, "想学点什么？", g_subject, SUBJECT_LEN - 1)) {
      g_state = ST_TYPING;
    } else {
      ai_str_copy(g_status, sizeof(g_status), "键盘暂时打不开，请稍后再试");
    }
    return;
  }
  ai_str_copy(g_subject, sizeof(g_subject), kSubjects[sel]);
  api->file_write("subject.txt", g_subject, (uint32_t)strlen(g_subject));
  g_state = ST_TODAY;
}

static uint32_t app_loop(const CpApi* api, const CpInput* in) {
  if (ai_job_settled(&g_job)) return CP_LOOP_IDLE;

  int repaint = 0;
  if (app_about_input(api, in, &g_about, g_state == ST_TODAY, &repaint)) {
    return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  }

  switch (g_state) {
    case ST_TYPING: {
      const int r = ai_input_poll(api, &g_input);
      if (r == 0) return CP_LOOP_IDLE;
      if (r == 1 && g_input.buf[0]) {
        ai_str_copy(g_subject, sizeof(g_subject), g_input.buf);
        api->file_write("subject.txt", g_subject, (uint32_t)strlen(g_subject));
        g_state = ST_TODAY;
      } else {
        g_state = g_subject[0] ? ST_TODAY : ST_PICK;
      }
      return CP_LOOP_RENDER;
    }

    case ST_THINKING:
      run_pending_call(api);
      return CP_LOOP_RENDER;

    case ST_PICK:
      if (in->released & CP_BTN_BACK) {
        if (!g_subject[0]) return CP_LOOP_EXIT;
        g_state = ST_TODAY;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_UP) g_pick_sel = (g_pick_sel + SUBJECT_COUNT - 1) % SUBJECT_COUNT;
      if (in->released & CP_BTN_DOWN) g_pick_sel = (g_pick_sel + 1) % SUBJECT_COUNT;
      if (in->released & CP_BTN_CONFIRM) pick_activate(api, g_pick_sel);
      return in->released ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_ARCHIVE:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_TODAY;
        return CP_LOOP_RENDER;
      }
      if (app_list_input(api, in, &g_list, g_seen_count)) {
        g_viewing = g_list.sel;
        load_lesson(api, g_list.sel);
        g_state = ST_TODAY;
      }
      return in->released ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_TODAY:
    default:
      if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
      if (in->released & CP_BTN_CONFIRM) {
        // Confirm generates when there is nothing for today, and replaces
        // today's card ("换一课") when there is.
        start_lesson(api);
        return CP_LOOP_RENDER;
      }
      if ((in->released & CP_BTN_LEFT) && g_seen_count > 0) {
        g_list.sel = 0;
        g_list.top = 0;
        g_state = ST_ARCHIVE;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_RIGHT) {
        g_pick_sel = 0;
        g_state = ST_PICK;
        return CP_LOOP_RENDER;
      }
      return (g_have_today && ai_pager_input(api, in, &g_pager)) ? CP_LOOP_RENDER : CP_LOOP_IDLE;
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

static const char* archive_row(int index, void* ctx) {
  (void)ctx;
  static char row[TITLE_LEN + 16];
  const long d = g_seen[index].date;
  if (d > 0) {
    cp_snprintf(row, sizeof(row), "%d.%d  %s", (int)((d / 100) % 100), (int)(d % 100), g_seen[index].title);
  } else {
    cp_snprintf(row, sizeof(row), "%s", g_seen[index].title);
  }
  return row;
}

static void render_today(const CpApi* api) {
  char right[32];
  if (g_have_today && ai_pager_scrollable(&g_pager)) {
    cp_snprintf(right, sizeof(right), "%d/%d", g_pager.page + 1, ai_pager_pages(&g_pager));
  } else if (g_today > 0) {
    cp_snprintf(right, sizeof(right), "%d.%d", (int)((g_today / 100) % 100), (int)(g_today % 100));
  } else {
    right[0] = 0;
  }
  const int viewing_old = g_viewing > 0;
  app_header(api, viewing_old ? "往期" : "每日一课", right);

  if (!g_have_today && g_viewing < 0) {
    const int w = api->screen_width(), h = api->screen_height();
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 50, "今天还没有课", 1, CP_TEXT_BOLD);
    api->draw_text_centered(CP_FONT_UI, w / 2, h / 2 - 10, g_subject[0] ? g_subject : "还没有选主题", 1,
                            CP_TEXT_REGULAR);
    api->draw_text_centered(CP_FONT_SMALL, w / 2, h / 2 + 26, "按确认生成，只联网这一次，之后离线也能看", 1,
                            CP_TEXT_REGULAR);
    app_hints(api, g_status[0] ? g_status : "确认=生成", "左=往期", "右=换主题", 0);
    return;
  }
  ai_pager_draw(api, &g_pager);
  app_hints(api, "上下翻页", "确认=换一课", "左=往期", "右=换主题");
}

static void render_pick(const CpApi* api) {
  const int y = app_header(api, "想学点什么？", 0);
  const int w = api->screen_width();
  const int rowH = 36;
  const int th = api->line_height(CP_FONT_UI);
  for (int i = 0; i < SUBJECT_COUNT; ++i) {
    const int ry = y + i * rowH;
    const int sel = (i == g_pick_sel);
    if (sel) api->fill_rect(8, ry, w - 16, rowH - 3, 1);
    api->draw_text(CP_FONT_UI, 24, ry + (rowH - 3 - th) / 2, kSubjects[i], sel ? 0 : 1, CP_TEXT_REGULAR);
  }
  app_hints(api, g_status[0] ? g_status : "确认选定", "返回", 0, 0);
}

static void on_render(const CpApi* api) {
  api->clear_screen();
  switch (g_state) {
    case ST_THINKING:
      ai_job_draw_busy(api, &g_job, "每日一课");
      break;
    case ST_PICK:
      render_pick(api);
      break;
    case ST_ARCHIVE:
      app_header(api, "往期", 0);
      app_list_draw_fn(api, &g_list, archive_row, 0, g_seen_count);
      app_hints(api, "确认打开", "返回", 0, 0);
      break;
    case ST_TYPING:
      app_message(api, "请输入想学的主题…");
      break;
    default:
      render_today(api);
      break;
  }
  if (g_about.open) app_about_draw(api, "每日一课");
}

static const CpApp kApp = {
    CP_ABI_VERSION, sizeof(CpApi), "每日一课", 10000, on_enter, on_loop, on_render, 0,
};

const CpApp* cp_app_entry(void);
const CpApp* cp_app_entry(void) { return &kApp; }
