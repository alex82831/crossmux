// 读书伴侣 — the AI app that needs no typing at all, because the content is
// already on the card.
//
// You browse to a .txt or .md file, read it in the normal paged way, and at
// any point press Confirm to ask about *this passage*: explain it, summarise
// it, turn it into discussion questions, untangle who is who, or pull the
// unfamiliar words out. The passage on screen is the prompt.
//
// EPUB is deliberately not here: it is a ZIP container, and inflate does not
// belong in a bare-C .eapp with no libc. Use the reader for EPUB; use this
// for the plain text and notes the card is full of.

#include "ai.h"

#define PATH_LEN 192
#define NAME_LEN 56
#define ENTRY_MAX 48
#define CHUNK_LEN 3072
#define STACK_MAX 64

typedef enum { ST_BROWSE = 0, ST_READ, ST_MENU, ST_THINKING, ST_ANSWER } State;

static AiConfig g_cfg;
static AiJob g_job;
static AiPager g_book;    // the passage on screen
static AiPager g_answer;  // kept separate so the reading position survives
static AppList g_list;
static AppAbout g_about;

static State g_state;
static char g_dir[PATH_LEN];
static char g_names[ENTRY_MAX][NAME_LEN];
static unsigned char g_isdir[ENTRY_MAX];
static int g_entries;

static char g_path[PATH_LEN];  // the open file
static char g_chunk[CHUNK_LEN];
static uint32_t g_offset;            // file offset of g_chunk
static uint32_t g_next_offset;       // where the following chunk starts
static uint32_t g_stack[STACK_MAX];  // chunk starts we came through
static int g_depth;
static int g_menu_sel;
static char g_status[80];

typedef struct {
  const char* label;
  const char* system;
} Action;

// The user message is the passage itself, verbatim — the instruction lives
// in the system prompt. That keeps one copy of the text in RAM instead of two.
static const Action kActions[] = {
    {"讲讲这一段",
     "用户发来的整条消息是一段书里的文字。指出这段里真正难懂的地方并讲清楚，"
     "不要复述原文，不要用 Markdown 记号，200 字以内。"},
    {"这段说了什么",
     "用户发来的整条消息是一段书里的文字。用最少的字说清它的要点，分点，"
     "每点一行，不要 Markdown 记号，120 字以内。"},
    {"提三个问题",
     "用户发来的整条消息是一段书里的文字。你在带读书会：针对它提三个值得讨论的问题，"
     "一行一个，只给问题，不要答案。"},
    {"人物与关系",
     "用户发来的整条消息是一段书里的文字。列出其中出现的人物，每行一个："
     "姓名——身份，以及与其他人的关系。没有人物就直接说没有。"},
    {"挑出生词",
     "用户发来的整条消息是一段书里的文字。从中挑出最多 8 个较难的词，每行一个："
     "词——简短释义。只挑真正不常见的。"},
};
#define ACTION_COUNT ((int)(sizeof(kActions) / sizeof(kActions[0])))

// --- path helpers ---------------------------------------------------------

static int ends_with_ci(const char* s, const char* suffix) {
  const int ls = (int)strlen(s), lf = (int)strlen(suffix);
  if (lf > ls) return 0;
  for (int i = 0; i < lf; ++i) {
    char a = s[ls - lf + i], b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (a != b) return 0;
  }
  return 1;
}

static int is_readable(const char* name) { return ends_with_ci(name, ".txt") || ends_with_ci(name, ".md"); }

static void join_path(const char* dir, const char* name, char* out, int cap) {
  const int slash = (dir[0] && dir[strlen(dir) - 1] == '/') ? 0 : 1;
  cp_snprintf(out, (unsigned)cap, slash ? "%s/%s" : "%s%s", dir, name);
}

static void parent_path(char* path) {
  int n = (int)strlen(path);
  while (n > 1 && path[n - 1] != '/') --n;
  if (n > 1) --n;  // drop the separator too
  path[n > 0 ? n : 1] = 0;
  if (path[0] == 0) {
    path[0] = '/';
    path[1] = 0;
  }
}

// --- directory listing ----------------------------------------------------

static void scan_dir(const CpApi* api) {
  int cap = 0;
  char* buf = ai_scratch(&cap);
  g_entries = 0;
  const int n = api->dir_list(g_dir, buf, (uint32_t)cap);
  if (n <= 0) return;
  buf[n < cap ? n : cap - 1] = 0;

  const char* p = buf;
  while (*p && g_entries < ENTRY_MAX) {
    char name[NAME_LEN];
    int w = 0;
    while (*p && *p != '\t' && *p != '\n' && w < NAME_LEN - 1) name[w++] = *p++;
    name[w] = 0;
    while (*p && *p != '\t' && *p != '\n') ++p;  // rest of the name if over-long
    int isdir = 0;
    if (*p == '\t') {
      ++p;
      while (*p && *p != '\t' && *p != '\n') ++p;  // size field
      if (*p == '\t') {
        ++p;
        isdir = (*p == 'D');
      }
    }
    while (*p && *p != '\n') ++p;
    if (*p == '\n') ++p;
    if (w == 0) continue;
    if (!isdir && !is_readable(name)) continue;  // only text this app can open
    ai_str_copy(g_names[g_entries], NAME_LEN, name);
    g_isdir[g_entries] = (unsigned char)isdir;
    ++g_entries;
  }
}

// --- reading --------------------------------------------------------------

static void layout_chunk(const CpApi* api) {
  const int y = APP_HEADER_H + 6;
  ai_pager_set(api, &g_book, g_chunk, CP_FONT_UI, 16, y + 4, api->screen_width() - 32,
               api->screen_height() - y - 4 - APP_FOOTER_H);
}

// Read one chunk at `at`, trimmed back to a clean break so no character and
// no word is split across the seam.
static int load_chunk(const CpApi* api, uint32_t at) {
  const int n = api->file_read_abs(g_path, at, g_chunk, sizeof(g_chunk) - 1);
  if (n <= 0) {
    g_chunk[0] = 0;
    g_next_offset = at;
    return 0;
  }
  int len = n;
  if (len >= (int)sizeof(g_chunk) - 1) {
    // Walk back off a partial UTF-8 sequence, then to the last line or space.
    while (len > 0 && ((unsigned char)g_chunk[len] & 0xC0) == 0x80) --len;
    int cut = len;
    for (int i = len - 1; i > len - 400 && i > 0; --i) {
      if (g_chunk[i] == '\n') {
        cut = i + 1;
        break;
      }
      if (cut == len && g_chunk[i] == ' ') cut = i + 1;
    }
    if (cut > 0) len = cut;
  }
  g_chunk[len] = 0;
  ai_text_normalize(g_chunk);
  g_offset = at;
  g_next_offset = at + (uint32_t)len;
  layout_chunk(api);
  return len > 0;
}

static void save_progress(const CpApi* api) {
  char line[PATH_LEN + 24];
  cp_snprintf(line, sizeof(line), "%u\t%s", (unsigned)g_offset, g_path);
  api->file_write("last.txt", line, (uint32_t)strlen(line));
}

static int restore_progress(const CpApi* api) {
  char line[PATH_LEN + 24];
  const int n = api->file_read("last.txt", line, sizeof(line));
  if (n <= 0) return 0;
  line[n < (int)sizeof(line) ? n : (int)sizeof(line) - 1] = 0;
  const char* p = line;
  uint32_t off = 0;
  while (*p >= '0' && *p <= '9') off = off * 10 + (uint32_t)(*p++ - '0');
  if (*p != '\t') return 0;
  ++p;
  ai_str_copy(g_path, PATH_LEN, p);
  ai_str_trim_end(g_path);
  if (!g_path[0]) return 0;
  g_depth = 0;
  return load_chunk(api, off);
}

static void open_file(const CpApi* api, const char* name) {
  join_path(g_dir, name, g_path, PATH_LEN);
  g_depth = 0;
  if (load_chunk(api, 0)) {
    g_state = ST_READ;
    save_progress(api);
  } else {
    ai_str_copy(g_status, sizeof(g_status), "这个文件打不开或者是空的");
  }
}

static void page_forward(const CpApi* api) {
  if (g_book.page + 1 < ai_pager_pages(&g_book)) {
    ++g_book.page;
    return;
  }
  const uint32_t from = g_offset;
  const int page = g_book.page;
  if (g_next_offset > from) {
    if (load_chunk(api, g_next_offset)) {
      if (g_depth < STACK_MAX) {
        g_stack[g_depth++] = from;
      } else {
        // Keep the most recent hops so paging back still works after a long
        // session; only the far tail becomes unreachable.
        memmove(g_stack, g_stack + 1, sizeof(g_stack) - sizeof(g_stack[0]));
        g_stack[STACK_MAX - 1] = from;
      }
      save_progress(api);
      return;
    }
    // The read past the seam failed: put the passage back where it was,
    // on the page the reader was actually looking at.
    load_chunk(api, from);
    g_book.page = page;
  }
  ai_str_copy(g_status, sizeof(g_status), "已经是最后一页了");
}

static void page_back(const CpApi* api) {
  if (g_book.page > 0) {
    --g_book.page;
    return;
  }
  if (g_depth == 0) return;
  load_chunk(api, g_stack[--g_depth]);
  g_book.page = ai_pager_pages(&g_book) - 1;
  save_progress(api);
}

// --- asking ---------------------------------------------------------------

static void start_action(const CpApi* api, int which) {
  (void)api;
  g_menu_sel = which;
  ai_job_start(&g_job, kActions[which].label);
  g_state = ST_THINKING;
}

static void run_pending_call(const CpApi* api) {
  const AiTurn turn = {"user", g_chunk};
  ai_job_pump(api, &g_job, &g_cfg, kActions[g_menu_sel].system, &turn, 1, 600);
  const int y = APP_HEADER_H + 6;
  ai_pager_set(api, &g_answer, g_job.result, CP_FONT_UI, 16, y + 8, api->screen_width() - 32,
               api->screen_height() - y - 8 - APP_FOOTER_H);
  g_state = ST_ANSWER;
}

// --- lifecycle -----------------------------------------------------------

static int32_t on_enter(const CpApi* api) {
  ai_config_load(api, &g_cfg);
  app_list_fit(api, &g_list, APP_HEADER_H + 10, 36);
  if (restore_progress(api)) {
    g_state = ST_READ;
  } else {
    g_path[0] = 0;  // the remembered book is gone; do not offer to resume it
    ai_str_copy(g_dir, sizeof(g_dir), "/books");
    scan_dir(api);
    if (g_entries == 0) {
      ai_str_copy(g_dir, sizeof(g_dir), "/");
      scan_dir(api);
    }
    g_state = ST_BROWSE;
  }
  if (!ai_config_ready(&g_cfg)) ai_str_copy(g_status, sizeof(g_status), "请先在「AI 助手」里配置服务和密钥");
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  if (ai_job_settled(&g_job)) return CP_LOOP_IDLE;

  int repaint = 0;
  if (app_about_input(api, in, &g_about, g_state == ST_BROWSE, &repaint)) {
    return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  }

  switch (g_state) {
    case ST_THINKING:
      run_pending_call(api);
      return CP_LOOP_RENDER;

    case ST_ANSWER:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_READ;
        return CP_LOOP_RENDER;
      }
      if ((in->released & CP_BTN_CONFIRM) && g_job.state == AI_FAILED) {
        ai_job_start(&g_job, kActions[g_menu_sel].label);
        g_state = ST_THINKING;
        return CP_LOOP_RENDER;
      }
      return ai_pager_input(api, in, &g_answer) ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_MENU:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_READ;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_UP) g_menu_sel = (g_menu_sel + ACTION_COUNT - 1) % ACTION_COUNT;
      if (in->released & CP_BTN_DOWN) g_menu_sel = (g_menu_sel + 1) % ACTION_COUNT;
      if (in->released & CP_BTN_CONFIRM) start_action(api, g_menu_sel);
      return in->released ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_READ:
      if (in->released & CP_BTN_BACK) {
        save_progress(api);
        ai_str_copy(g_dir, sizeof(g_dir), g_path);
        parent_path(g_dir);
        scan_dir(api);
        g_state = ST_BROWSE;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_CONFIRM) {
        g_menu_sel = 0;
        g_state = ST_MENU;
        return CP_LOOP_RENDER;
      }
      if (in->released & (CP_BTN_DOWN | CP_BTN_RIGHT | CP_BTN_PAGE_FORWARD)) {
        g_status[0] = 0;
        page_forward(api);
        return CP_LOOP_RENDER;
      }
      if (in->released & (CP_BTN_UP | CP_BTN_LEFT | CP_BTN_PAGE_BACK)) {
        g_status[0] = 0;
        page_back(api);
        return CP_LOOP_RENDER;
      }
      return CP_LOOP_IDLE;

    case ST_BROWSE:
    default:
      if (in->released & CP_BTN_BACK) {
        if (strcmp(g_dir, "/") == 0) return CP_LOOP_EXIT;
        parent_path(g_dir);
        g_list.sel = 0;
        g_list.top = 0;
        scan_dir(api);
        return CP_LOOP_RENDER;
      }
      if ((in->released & CP_BTN_RIGHT) && g_path[0]) {
        g_state = ST_READ;  // back to where we were reading
        return CP_LOOP_RENDER;
      }
      if (in->released) g_status[0] = 0;
      if (app_list_input(api, in, &g_list, g_entries)) {
        if (g_entries == 0) return CP_LOOP_IDLE;
        if (g_isdir[g_list.sel]) {
          char next[PATH_LEN];
          join_path(g_dir, g_names[g_list.sel], next, sizeof(next));
          ai_str_copy(g_dir, sizeof(g_dir), next);
          g_list.sel = 0;
          g_list.top = 0;
          scan_dir(api);
        } else {
          open_file(api, g_names[g_list.sel]);
        }
      }
      return in->released ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  }
}

// --- render --------------------------------------------------------------

static const char* browse_row(int index, void* ctx) {
  (void)ctx;
  static char row[NAME_LEN + 8];
  cp_snprintf(row, sizeof(row), g_isdir[index] ? "[ %s ]" : "%s", g_names[index]);
  return row;
}

static void render_browse(const CpApi* api) {
  char right[24];
  ai_str_clip(right, sizeof(right), g_dir, 22);
  app_header(api, "读书伴侣", right);
  if (g_entries == 0) {
    const int w = api->screen_width(), h = api->screen_height();
    api->draw_text_centered(CP_FONT_UI, w / 2, h / 2 - 20, "这个目录里没有 txt 或 md 文件", 1, CP_TEXT_REGULAR);
    api->draw_text_centered(CP_FONT_SMALL, w / 2, h / 2 + 14, "把文本放到 /books 目录即可", 1, CP_TEXT_REGULAR);
  } else {
    app_list_draw_fn(api, &g_list, browse_row, 0, g_entries);
  }
  app_hints(api, g_status[0] ? g_status : "确认打开", "返回上级", g_path[0] ? "右=继续读" : "", 0);
}

static void render_read(const CpApi* api) {
  char right[32];
  cp_snprintf(right, sizeof(right), "%d/%d", g_book.page + 1, ai_pager_pages(&g_book));
  char title[40];
  const char* base = g_path;
  for (const char* p = g_path; *p; ++p) {
    if (*p == '/') base = p + 1;
  }
  ai_str_clip(title, sizeof(title), base, 34);
  app_header(api, title, right);
  ai_pager_draw(api, &g_book);
  app_hints(api, g_status[0] ? g_status : "上下翻页", "确认=问 AI", "返回=选书", 0);
}

static void render_menu(const CpApi* api) {
  const int y = app_header(api, "问 AI（用当前这一段）", 0);
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
  app_hints(api, "确认发送", "返回", "只发送屏幕上这一段", 0);
}

static void render_answer(const CpApi* api) {
  char right[32];
  if (ai_pager_scrollable(&g_answer)) {
    cp_snprintf(right, sizeof(right), "%d/%d", g_answer.page + 1, ai_pager_pages(&g_answer));
  } else {
    right[0] = 0;
  }
  app_header(api, g_job.state == AI_DONE ? kActions[g_menu_sel].label : "没能完成", right);
  ai_pager_draw(api, &g_answer);
  if (g_job.state == AI_DONE) {
    app_hints(api, "上下翻页", "返回继续读", 0, 0);
  } else {
    app_hints(api, "确认重试", "返回继续读", 0, 0);
  }
}

static void on_render(const CpApi* api) {
  api->clear_screen();
  switch (g_state) {
    case ST_THINKING:
      ai_job_draw_busy(api, &g_job, "读书伴侣");
      break;
    case ST_ANSWER:
      render_answer(api);
      break;
    case ST_MENU:
      render_menu(api);
      break;
    case ST_READ:
      render_read(api);
      break;
    default:
      render_browse(api);
      break;
  }
  if (g_about.open) app_about_draw(api, "读书伴侣");
}

static const CpApp kApp = {
    CP_ABI_VERSION, sizeof(CpApi), "读书伴侣", 10000, on_enter, on_loop, on_render, 0,
};

const CpApp* cp_app_entry(void);
const CpApp* cp_app_entry(void) { return &kApp; }
