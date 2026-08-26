// AI 助手 — the hub app for CrossPoint's AI family.
//
// It owns the shared configuration (service address, key, model, answer
// language) that every other AI app reads, so the key is typed exactly once
// on a device where typing is the expensive part.
//
// The interaction is built around that same constraint: the home screen is a
// list of one-tap prompts, several of which need no typing at all, and any
// answer can be followed up without restating the topic.

#include "ai.h"

// --- state ---------------------------------------------------------------

typedef enum {
  ST_HOME = 0,
  ST_TYPING,   // host keyboard is up
  ST_THINKING, // busy screen painted, call runs next frame
  ST_ANSWER,
  ST_SETTINGS,
  ST_SET_EDIT,
} State;

// Which setting a keyboard session is editing.
typedef enum { ED_NONE = 0, ED_PROMPT, ED_FOLLOWUP, ED_URL, ED_KEY, ED_MODEL, ED_LANG } EditTarget;

#define HIST_TURNS 6
#define HIST_LEN 460

static AiConfig g_cfg;
static AiJob g_job;
static AiInput g_input;
static AiPager g_pager;
static AppList g_list;
static AppAbout g_about;

static State g_state;
static EditTarget g_edit;
static char g_question[AI_INPUT_MAX];   // what was actually sent
static char g_hist[HIST_TURNS][HIST_LEN];
static char g_roles[HIST_TURNS][12];
static int g_hist_count;
static char g_sys[256];
static char g_endpoint[AI_URL_LEN + 24];
static char g_status[96];
static int g_settings_sel;

// One-tap prompts. The first entry opens the keyboard; the rest are either
// prefixes the user completes, or complete questions needing no typing.
typedef struct {
  const char* label;
  const char* prefix;  // pre-filled in the keyboard; NULL = send as-is
  const char* ask;     // used when prefix is NULL
} Preset;

static const Preset kPresets[] = {
    {"自由提问…", "", 0},
    {"解释一个概念…", "用简单的话解释：", 0},
    {"翻译成英文…", "翻译成地道英文：", 0},
    {"翻译成中文…", "翻译成通顺中文：", 0},
    {"帮我润色一段话…", "帮我润色，保持原意：", 0},
    {"一句话总结…", "用一句话总结：", 0},
    {"讲个冷知识", 0, "讲一个我大概率不知道的冷知识，说清楚它为什么成立。"},
    {"今天读点什么", 0, "推荐三本值得读的书，各用一句话说明它解决什么问题。"},
    {"来道思维题", 0, "出一道有意思的逻辑推理题，先只给题目，答案放在最后并标注「答案」。"},
};
#define PRESET_COUNT ((int)(sizeof(kPresets) / sizeof(kPresets[0])))

#define HOME_COUNT (PRESET_COUNT + 1)  // + 设置

static const char* kSettingLabels[] = {"服务地址", "接口密钥", "模型名称", "回答语言", "测试连接", "保存并返回"};
#define SETTING_COUNT ((int)(sizeof(kSettingLabels) / sizeof(kSettingLabels[0])))

// --- helpers -------------------------------------------------------------

static void build_system_prompt(void) {
  ai_str_copy(g_sys, sizeof(g_sys), "你是一台电子墨水屏阅读器上的助手。请用");
  ai_str_append(g_sys, sizeof(g_sys), g_cfg.lang[0] ? g_cfg.lang : "中文");
  ai_str_append(g_sys, sizeof(g_sys),
                "回答，直接给结论，不要客套。屏幕很小：控制在 300 字以内，"
                "分点时每点另起一行，不要使用表格和 Markdown 记号。");
}

static void history_push(const char* role, const char* text) {
  if (g_hist_count == HIST_TURNS) {
    // Drop the oldest exchange (two entries) so context stays paired.
    for (int i = 2; i < HIST_TURNS; ++i) {
      ai_str_copy(g_hist[i - 2], HIST_LEN, g_hist[i]);
      ai_str_copy(g_roles[i - 2], 12, g_roles[i]);
    }
    g_hist_count -= 2;
  }
  ai_str_copy(g_roles[g_hist_count], 12, role);
  ai_str_copy(g_hist[g_hist_count], HIST_LEN, text);
  ++g_hist_count;
}

// Send whatever is in g_question, carrying the conversation so far.
static void ask(const CpApi* api, const char* note) {
  (void)api;
  history_push("user", g_question);
  ai_job_start(&g_job, note);
  g_state = ST_THINKING;
}

static void run_pending_call(const CpApi* api) {
  AiTurn turns[HIST_TURNS];
  for (int i = 0; i < g_hist_count; ++i) {
    turns[i].role = g_roles[i];
    turns[i].text = g_hist[i];
  }
  build_system_prompt();
  ai_job_pump(api, &g_job, &g_cfg, g_sys, turns, g_hist_count, 700);

  if (g_job.state == AI_DONE) {
    history_push("assistant", g_job.result);
  } else {
    // A failed turn must not poison the context of the next attempt.
    if (g_hist_count > 0) --g_hist_count;
  }
  const int y = APP_HEADER_H + 6;
  ai_pager_set(api, &g_pager, g_job.result, CP_FONT_UI, 16, y + 30, api->screen_width() - 32,
               api->screen_height() - y - 30 - APP_FOOTER_H);
  g_state = ST_ANSWER;
}

static void open_keyboard(const CpApi* api, EditTarget target, const char* title, const char* initial, int max_len) {
  g_edit = target;
  if (ai_input_begin(api, &g_input, title, initial, max_len)) {
    g_state = ST_TYPING;
  } else {
    ai_str_copy(g_status, sizeof(g_status), "键盘暂时打不开，请稍后再试");
    g_edit = ED_NONE;
  }
}

static void test_connection(const CpApi* api) {
  (void)api;
  g_hist_count = 0;
  ai_str_copy(g_question, sizeof(g_question), "回复两个字：正常");
  history_push("user", g_question);
  ai_job_start(&g_job, "正在测试服务连接…");
  g_state = ST_THINKING;
}

// --- input ---------------------------------------------------------------

static void home_activate(const CpApi* api, int sel) {
  if (sel == PRESET_COUNT) {
    ai_endpoint(&g_cfg, g_endpoint, sizeof(g_endpoint));
    g_settings_sel = 0;
    g_state = ST_SETTINGS;
    return;
  }
  const Preset* p = &kPresets[sel];
  if (p->prefix) {
    open_keyboard(api, ED_PROMPT, p->label, p->prefix, AI_INPUT_MAX - 1);
    return;
  }
  g_hist_count = 0;
  ai_str_copy(g_question, sizeof(g_question), p->ask);
  ask(api, p->label);
}

static void settings_activate(const CpApi* api, int sel) {
  switch (sel) {
    case 0:
      open_keyboard(api, ED_URL, "服务地址（OpenAI 兼容）", g_cfg.base_url, AI_URL_LEN - 1);
      break;
    case 1:
      open_keyboard(api, ED_KEY, "接口密钥", g_cfg.api_key, AI_KEY_LEN - 1);
      break;
    case 2:
      open_keyboard(api, ED_MODEL, "模型名称", g_cfg.model, AI_MODEL_LEN - 1);
      break;
    case 3:
      open_keyboard(api, ED_LANG, "回答语言", g_cfg.lang, AI_LANG_LEN - 1);
      break;
    case 4:
      if (!ai_config_ready(&g_cfg)) {
        ai_str_copy(g_status, sizeof(g_status), "地址、密钥、模型都填好才能测试");
        break;
      }
      ai_config_save(api, &g_cfg);
      test_connection(api);
      break;
    default:
      ai_str_copy(g_status, sizeof(g_status), ai_config_save(api, &g_cfg) ? "已保存" : "保存失败，请检查 SD 卡");
      g_state = ST_HOME;
      break;
  }
}

static void apply_typed_text(const CpApi* api, const char* text) {
  switch (g_edit) {
    case ED_PROMPT:
      g_hist_count = 0;
      ai_str_copy(g_question, sizeof(g_question), text);
      ask(api, "正在回答你的问题…");
      break;
    case ED_FOLLOWUP:
      ai_str_copy(g_question, sizeof(g_question), text);
      ask(api, "正在接着回答…");
      break;
    case ED_URL:
      ai_str_copy(g_cfg.base_url, AI_URL_LEN, text);
      ai_endpoint(&g_cfg, g_endpoint, sizeof(g_endpoint));
      g_state = ST_SETTINGS;
      break;
    case ED_KEY:
      ai_str_copy(g_cfg.api_key, AI_KEY_LEN, text);
      g_state = ST_SETTINGS;
      break;
    case ED_MODEL:
      ai_str_copy(g_cfg.model, AI_MODEL_LEN, text);
      g_state = ST_SETTINGS;
      break;
    case ED_LANG:
      ai_str_copy(g_cfg.lang, AI_LANG_LEN, text);
      g_state = ST_SETTINGS;
      break;
    default:
      g_state = ST_HOME;
      break;
  }
  g_edit = ED_NONE;
}

static int32_t on_enter(const CpApi* api) {
  ai_config_load(api, &g_cfg);
  ai_endpoint(&g_cfg, g_endpoint, sizeof(g_endpoint));
  app_list_fit(api, &g_list, APP_HEADER_H + 10, 38);
  g_state = ST_HOME;
  if (!ai_config_ready(&g_cfg)) {
    ai_str_copy(g_status, sizeof(g_status), "首次使用：请先进入「设置」填入密钥");
  }
  return 0;
}

static uint32_t app_loop(const CpApi* api, const CpInput* in) {
  // Presses that landed while the HTTP call blocked must not act on the
  // screen that replaced the busy panel.
  if (ai_job_settled(&g_job)) return CP_LOOP_IDLE;

  int repaint = 0;
  if (app_about_input(api, in, &g_about, g_state == ST_HOME, &repaint)) {
    return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;
  }

  switch (g_state) {
    case ST_TYPING: {
      const int r = ai_input_poll(api, &g_input);
      if (r == 0) return CP_LOOP_IDLE;
      if (r == 1) {
        apply_typed_text(api, g_input.buf);
      } else {
        g_state = (g_edit >= ED_URL) ? ST_SETTINGS : ST_HOME;
        g_edit = ED_NONE;
      }
      return CP_LOOP_RENDER;
    }

    case ST_THINKING:
      run_pending_call(api);
      return CP_LOOP_RENDER;

    case ST_ANSWER:
      if (in->released & CP_BTN_BACK) {
        g_state = ST_HOME;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_CONFIRM) {
        if (g_job.state != AI_DONE) {
          // Retry the same question rather than making the user retype it.
          ai_job_start(&g_job, "正在重试…");
          g_state = ST_THINKING;
          history_push("user", g_question);
          return CP_LOOP_RENDER;
        }
        open_keyboard(api, ED_FOLLOWUP, "追问（会带上前面的对话）", "", AI_INPUT_MAX - 1);
        return CP_LOOP_RENDER;
      }
      return ai_pager_input(api, in, &g_pager) ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_SETTINGS:
      if (in->released & CP_BTN_BACK) {
        ai_config_save(api, &g_cfg);
        ai_str_copy(g_status, sizeof(g_status), "已保存");
        g_state = ST_HOME;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_UP) g_settings_sel = (g_settings_sel + SETTING_COUNT - 1) % SETTING_COUNT;
      if (in->released & CP_BTN_DOWN) g_settings_sel = (g_settings_sel + 1) % SETTING_COUNT;
      if (in->released & CP_BTN_CONFIRM) settings_activate(api, g_settings_sel);
      return in->released ? CP_LOOP_RENDER : CP_LOOP_IDLE;

    case ST_HOME:
    default:
      if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
      if (app_list_input(api, in, &g_list, HOME_COUNT)) {
        g_status[0] = 0;
        home_activate(api, g_list.sel);
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

static const char* home_row(int index, void* ctx) {
  (void)ctx;
  return index < PRESET_COUNT ? kPresets[index].label : "设置…";
}

static void render_home(const CpApi* api) {
  const char* right = ai_config_ready(&g_cfg) ? g_cfg.model : "未配置";
  app_header(api, "AI 助手", right);
  app_list_draw_fn(api, &g_list, home_row, 0, HOME_COUNT);
  if (g_status[0]) {
    app_hints(api, g_status, 0, 0, 0);
  } else {
    app_hints(api, "上下选择", "确认打开", "长按返回=关于", 0);
  }
}

static void render_answer(const CpApi* api) {
  const int ok = g_job.state == AI_DONE;
  char right[32];
  if (ai_pager_scrollable(&g_pager)) {
    cp_snprintf(right, sizeof(right), "%d/%d", g_pager.page + 1, ai_pager_pages(&g_pager));
  } else {
    right[0] = 0;
  }
  const int y = app_header(api, ok ? "回答" : "没能完成", right);

  // Echo the question so a paged answer still says what it answers.
  char q[72];
  ai_str_clip(q, sizeof(q), g_question, 60);
  api->draw_text(CP_FONT_SMALL, 16, y, q, 1, CP_TEXT_BOLD);
  api->draw_line(16, y + 22, api->screen_width() - 16, y + 22, 1);

  ai_pager_draw(api, &g_pager);
  if (ok) {
    app_hints(api, "确认追问", "上下翻页", "返回", 0);
  } else {
    app_hints(api, "确认重试", "返回", 0, 0);
  }
}

static void render_settings(const CpApi* api) {
  const int y = app_header(api, "设置", 0);
  const int w = api->screen_width();
  const int rowH = 44;
  const int lh = api->line_height(CP_FONT_SMALL);

  for (int i = 0; i < SETTING_COUNT; ++i) {
    const int ry = y + i * rowH;
    const int sel = (i == g_settings_sel);
    if (sel) api->fill_rect(8, ry, w - 16, rowH - 4, 1);
    api->draw_text(CP_FONT_UI, 20, ry + 4, kSettingLabels[i], sel ? 0 : 1, CP_TEXT_REGULAR);

    // Second line: the current value, with the key masked.
    char value[64];
    value[0] = 0;
    switch (i) {
      case 0:
        ai_str_clip(value, sizeof(value), g_cfg.base_url, 56);
        break;
      case 1:
        if (g_cfg.api_key[0]) {
          const int n = (int)strlen(g_cfg.api_key);
          cp_snprintf(value, sizeof(value), "已设置（%d 位）", n);
        } else {
          ai_str_copy(value, sizeof(value), "未设置");
        }
        break;
      case 2:
        ai_str_clip(value, sizeof(value), g_cfg.model, 56);
        break;
      case 3:
        ai_str_clip(value, sizeof(value), g_cfg.lang, 56);
        break;
      case 4:
        ai_str_clip(value, sizeof(value), g_endpoint, 56);
        break;
      default:
        break;
    }
    if (value[0]) {
      api->draw_text(CP_FONT_SMALL, 20, ry + rowH - 6 - lh, value, sel ? 0 : 1, CP_TEXT_REGULAR);
    }
  }
  if (g_status[0]) {
    app_hints(api, g_status, 0, 0, 0);
  } else {
    app_hints(api, "确认编辑", "返回保存", "密钥只存在本机 SD 卡", 0);
  }
}

static void on_render(const CpApi* api) {
  api->clear_screen();
  switch (g_state) {
    case ST_THINKING:
      ai_job_draw_busy(api, &g_job, "AI 助手");
      break;
    case ST_ANSWER:
      render_answer(api);
      break;
    case ST_SETTINGS:
      render_settings(api);
      break;
    case ST_TYPING:
      app_message(api, "请在键盘上输入…");
      break;
    default:
      render_home(api);
      break;
  }
  if (g_about.open) app_about_draw(api, "AI 助手");
}

static const CpApp kApp = {
    CP_ABI_VERSION, sizeof(CpApi), "AI 助手", 10000, on_enter, on_loop, on_render, 0,
};

const CpApp* cp_app_entry(void);
const CpApp* cp_app_entry(void) { return &kApp; }
