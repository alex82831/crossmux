// 灵感整理 — an inbox for half-thoughts, and something that makes sense of
// them later.
//
// The value is in the asymmetry. Capture has to be nearly free: one short
// line, dated automatically, and you are out. Making sense of the pile is
// the expensive part, and it is exactly what a model is good at — so it
// happens on demand, over everything you have written, not note by note.
//
// "找出主线" is the one to try: over twenty scattered jottings it tends to
// name the thing you have been circling without noticing.

#include "ai.h"

#define NOTES_CAP 3072
#define NOTE_MAX 64
#define ROW_LEN 96
#define OUTLINE_LEN 2048

typedef enum { ST_LIST = 0, ST_TYPING, ST_MENU, ST_THINKING, ST_ANSWER } State;

static AiConfig g_cfg;
static AiJob g_job;
static AiInput g_input;
static AiPager g_pager;
static AppList g_list;
static AppAbout g_about;

static State g_state;
static char g_notes[NOTES_CAP];
static unsigned short g_line[NOTE_MAX + 1];
static int g_count;
static char g_one[288];  // the single note sent by 接着想
static char g_outline[OUTLINE_LEN];
static char g_status[80];
static int g_menu_sel;
static int g_confirm_delete;

typedef struct {
  const char* label;
  const char* system;
  int whole;  // 1 = send every note, 0 = send the highlighted one
} Action;

static const Action kActions[] = {
    {"整理成提纲",
     "用户发来的整条消息是一堆随手记下的想法，每行一条，行首是日期。把它们归到几个主题下，"
     "输出一份提纲：主题一行，其下用短句列出属于它的想法（可以合并同义的）。"
     "不要 Markdown 记号，不要评价，300 字以内。",
     1},
    {"提取待办",
     "用户发来的整条消息是一堆随手记下的想法，每行一条，行首是日期。只挑出其中真正需要行动的，"
     "每行一条，写成可以直接去做的动作，并按值得先做的顺序排列。没有就直接说没有。不要 Markdown 记号。",
     1},
    {"找出主线",
     "用户发来的整条消息是一堆随手记下的想法，每行一条，行首是日期。告诉我：这些想法背后反复出现的关注点是什么，"
     "我可能自己没意识到的是什么，以及接下来最值得往哪个方向走。三段，每段一行，250 字以内，不要 Markdown 记号。",
     1},
    {"接着想这一条",
     "用户发来的是一条随手记下的想法。给出三个具体的推进方向，一行一个，每行都要能直接动手，"
     "不要空泛的建议，不要 Markdown 记号。",
     0},
    {"看上次整理", 0, 0},
};
#define ACTION_COUNT ((int)(sizeof(kActions) / sizeof(kActions[0])))

// --- notes ---------------------------------------------------------------

static void reindex(void) {
  g_count = 0;
  g_line[0] = 0;
  const int n = (int)strlen(g_notes);
  for (int i = 0; i < n && g_count < NOTE_MAX; ++i) {
    if (g_notes[i] == '\n') g_line[++g_count] = (unsigned short)(i + 1);
  }
  // A trailing newline closes the last note; anything after it is a partial.
  if (g_count > 0 && g_line[g_count] >= n) return;
  if (n > 0 && g_count < NOTE_MAX) g_line[++g_count] = (unsigned short)n;
}

// Copy note `i` (0 = oldest) without its newline.
static void note_text(int i, char* out, int cap) {
  const int a = g_line[i];
  int b = g_line[i + 1];
  while (b > a && (g_notes[b - 1] == '\n' || g_notes[b - 1] == '\r')) --b;
  int n = b - a;
  if (n > cap - 1) n = cap - 1;
  if (n < 0) n = 0;
  memcpy(out, g_notes + a, (size_t)n);
  out[n] = 0;
}

static void notes_load(const CpApi* api) {
  const int n = api->file_read("notes.txt", g_notes, sizeof(g_notes));
  g_notes[n > 0 && n < (int)sizeof(g_notes) ? n : 0] = 0;
  ai_text_normalize(g_notes);
  reindex();
}

static void notes_save(const CpApi* api) { api->file_write("notes.txt", g_notes, (uint32_t)strlen(g_notes)); }

static void note_add(const CpApi* api, const char* text) {
  char dated[ROW_LEN + 16];
  int32_t mo = 0, d = 0;
  if (api->rtc_now(0, &mo, &d, 0, 0, 0, 0)) {
    cp_snprintf(dated, sizeof(dated), "%d.%d %s", (int)mo, (int)d, text);
  } else {
    cp_snprintf(dated, sizeof(dated), "%s", text);
  }
  const int need = (int)strlen(dated) + 1;

  // Make room by dropping whole notes from the front, oldest first.
  int len = (int)strlen(g_notes);
  while ((len + need >= (int)sizeof(g_notes) || g_count >= NOTE_MAX) && g_count > 0) {
    const int cut = g_line[1];
    memmove(g_notes, g_notes + cut, (size_t)(len - cut + 1));
    len -= cut;
    reindex();
  }
  if (len + need >= (int)sizeof(g_notes)) return;

  memcpy(g_notes + len, dated, (size_t)(need - 1));
  g_notes[len + need - 1] = '\n';
  g_notes[len + need] = 0;
  reindex();
  notes_save(api);
}

static void note_delete(const CpApi* api, int i) {
  const int a = g_line[i], b = g_line[i + 1];
  const int len = (int)strlen(g_notes);
  memmove(g_notes + a, g_notes + b, (size_t)(len - b + 1));
  reindex();
  notes_save(api);
}

// The list shows newest first; the file stays chronological because that is
// the order the model should read them in.
static int display_to_note(int row) { return g_count - 1 - row; }

// --- flow ----------------------------------------------------------------

static void layout_answer(const CpApi* api, const char* text) {
  const int y = APP_HEADER_H + 6;
  ai_pager_set(api, &g_pager, text, CP_FONT_UI, 16, y + 8, api->screen_width() - 32,
               api->screen_height() - y - 8 - APP_FOOTER_H);
}

static void run_pending_call(const CpApi* api) {
  const Action* act = &kActions[g_menu_sel];
  const AiTurn turn = {"user", act->whole ? g_notes : g_one};
  ai_job_pump(api, &g_job, &g_cfg, act->system, &turn, 1, 700);
  if (g_job.state == AI_DONE) {
    ai_str_copy(g_outline, sizeof(g_outline), g_job.result);
    api->file_write("outline.txt", g_outline, (uint32_t)strlen(g_outline));
    layout_answer(api, g_outline);
  } else {
    layout_answer(api, g_job.result);
  }
  g_state = ST_ANSWER;
}

static void start_action(const CpApi* api, int which) {
  if (which == ACTION_COUNT - 1) {
    const int n = api->file_read("outline.txt", g_outline, sizeof(g_outline));
    if (n <= 0) {
      ai_str_copy(g_status, sizeof(g_status), "还没有整理过");
      g_state = ST_LIST;
      return;
    }
    g_outline[n < (int)sizeof(g_outline) ? n : (int)sizeof(g_outline) - 1] = 0;
    g_job.state = AI_DONE;  // an offline read is a good result
    layout_answer(api, g_outline);
    g_menu_sel = which;
    g_state = ST_ANSWER;
    return;
  }
  if (g_count == 0) {
    ai_str_copy(g_status, sizeof(g_status), "先记几条再整理");
    g_state = ST_LIST;
    return;
  }
  if (!kActions[which].whole) {
    const int sel = g_list.sel - 1;
    if (sel < 0) {
      ai_str_copy(g_status, sizeof(g_status), "请先在列表里选中一条");
      g_state = ST_LIST;
      return;
    }
    note_text(display_to_note(sel), g_one, sizeof(g_one));
  }
  g_menu_sel = which;
  ai_job_start(&g_job, kActions[which].label);
  g_state = ST_THINKING;
}

static int32_t on_enter(const CpApi* api) {
  ai_config_load(api, &g_cfg);
  notes_load(api);
  app_list_fit(api, &g_list, APP_HEADER_H + 10, 36);
  g_state = ST_LIST;
  if (!ai_config_ready(&g_cfg)) ai_str_copy(g_status, sizeof(g_status), "记录不需要联网；整理需要先配置 AI 服务");
  return 0;
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
        ai_text_normalize(g_input.buf);
        // Notes are one line each; a pasted newline would split the record.
        for (char* p = g_input.buf; *p; ++p) {
          if (*p == '\n') *p = ' ';
        }
        note_add(api, g_input.buf);
        ai_str_copy(g_status, sizeof(g_status), "已记下");
      }
      g_state = ST_LIST;
      return CP_LOOP_RENDER;
    }

    case ST_THINKING:
      run_pending_call(api);
      return CP_LOOP_RENDER;

    case ST_ANSWER:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_LIST;
        return CP_LOOP_RENDER;
      }
      if ((in->released & CP_BTN_CONFIRM) && g_job.state == AI_FAILED) {
        ai_job_start(&g_job, kActions[g_menu_sel].label);
        g_state = ST_THINKING;
        return CP_LOOP_RENDER;
      }
      return ai_pager_input(api, in, &g_pager) ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_MENU:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_LIST;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_UP) g_menu_sel = (g_menu_sel + ACTION_COUNT - 1) % ACTION_COUNT;
      if (in->released & CP_BTN_DOWN) g_menu_sel = (g_menu_sel + 1) % ACTION_COUNT;
      if (in->released & CP_BTN_CONFIRM) start_action(api, g_menu_sel);
      return in->released ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_LIST:
    default:
      if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
      if (in->released & CP_BTN_LEFT) {
        g_menu_sel = 0;
        g_state = ST_MENU;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_RIGHT) {
        if (g_list.sel > 0 && g_list.sel <= g_count) {
          if (g_confirm_delete == g_list.sel) {
            note_delete(api, display_to_note(g_list.sel - 1));
            g_confirm_delete = 0;
            ai_str_copy(g_status, sizeof(g_status), "已删除");
          } else {
            g_confirm_delete = g_list.sel;
            ai_str_copy(g_status, sizeof(g_status), "再按一次「右」确认删除");
          }
        }
        return CP_LOOP_RENDER;
      }
      if (in->released) {
        g_confirm_delete = 0;
        g_status[0] = 0;
      }
      if (app_list_input(api, in, &g_list, g_count + 1)) {
        if (g_list.sel == 0) {
          if (ai_input_begin(api, &g_input, "记一条（短一点就好）", "", ROW_LEN)) {
            g_state = ST_TYPING;
          } else {
            ai_str_copy(g_status, sizeof(g_status), "键盘暂时打不开，请稍后再试");
          }
        } else {
          note_text(display_to_note(g_list.sel - 1), g_one, sizeof(g_one));
          g_job.state = AI_DONE;
          g_menu_sel = ACTION_COUNT - 1;
          layout_answer(api, g_one);
          g_state = ST_ANSWER;
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
  static char row[ROW_LEN + 24];
  if (index == 0) return "记一条…";
  note_text(display_to_note(index - 1), row, sizeof(row));
  return row;
}

static void render_list(const CpApi* api) {
  char right[24];
  cp_snprintf(right, sizeof(right), "%d 条", g_count);
  app_header(api, "灵感整理", right);
  app_list_draw_fn(api, &g_list, list_row, 0, g_count + 1);
  if (g_status[0]) {
    app_hints(api, g_status, 0, 0, 0);
  } else {
    app_hints(api, "确认记录", "左=整理", "右=删除", "长按返回=关于");
  }
}

static void render_menu(const CpApi* api) {
  const int y = app_header(api, "整理", 0);
  const int w = api->screen_width();
  const int rowH = 44;
  const int th = api->line_height(CP_FONT_UI_LARGE);
  for (int i = 0; i < ACTION_COUNT; ++i) {
    const int ry = y + 8 + i * rowH;
    const int sel = (i == g_menu_sel);
    if (sel)
      api->fill_rect(24, ry, w - 48, rowH - 6, 1);
    else
      api->draw_rect(24, ry, w - 48, rowH - 6, 1);
    api->draw_text(CP_FONT_UI_LARGE, 44, ry + (rowH - 6 - th) / 2, kActions[i].label, sel ? 0 : 1, CP_TEXT_REGULAR);
  }
  app_hints(api, "确认执行", "返回", "整理结果会存在本机", 0);
}

static void render_answer(const CpApi* api) {
  char right[32];
  if (ai_pager_scrollable(&g_pager)) {
    cp_snprintf(right, sizeof(right), "%d/%d", g_pager.page + 1, ai_pager_pages(&g_pager));
  } else {
    right[0] = 0;
  }
  app_header(api, g_job.state == AI_DONE ? kActions[g_menu_sel].label : "没能完成", right);
  ai_pager_draw(api, &g_pager);
  if (g_job.state == AI_DONE) {
    app_hints(api, "上下翻页", "返回", 0, 0);
  } else {
    app_hints(api, "确认重试", "返回", 0, 0);
  }
}

static void on_render(const CpApi* api) {
  api->clear_screen();
  switch (g_state) {
    case ST_THINKING:
      ai_job_draw_busy(api, &g_job, "灵感整理");
      break;
    case ST_ANSWER:
      render_answer(api);
      break;
    case ST_MENU:
      render_menu(api);
      break;
    case ST_TYPING:
      app_message(api, "请输入这一条…");
      break;
    default:
      render_list(api);
      break;
  }
  if (g_about.open) app_about_draw(api, "灵感整理");
}

static const CpApp kApp = {
    CP_ABI_VERSION, sizeof(CpApi), "灵感整理", 10000, on_enter, on_loop, on_render, 0,
};

const CpApp* cp_app_entry(void);
const CpApp* cp_app_entry(void) { return &kApp; }
