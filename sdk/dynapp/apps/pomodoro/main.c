// 番茄钟 — Pomodoro timer. Three schedules, long break every 4th round,
// today/total stats persisted. Ported to .eapp from the firmware app.

#include "app.h"

typedef struct {
  const char* name;
  int focus, brk, longBrk;  // minutes
} Schedule;

static const Schedule kSched[3] = {
    {"25-5-15", 25, 5, 15},
    {"30-5-20", 30, 5, 20},
    {"45-10-20", 45, 10, 20},
};

enum { PHASE_FOCUS, PHASE_BREAK, PHASE_LONG };

static int g_sched;
static int g_phase;
static int g_round;       // completed focus rounds in this cycle (0..3)
static int g_running;
static uint32_t g_end_ms;      // millis when the current phase ends
static uint32_t g_remain_ms;   // frozen remaining while paused
static uint32_t g_today, g_total;  // completed focus sessions
static AppAbout g_about;

typedef struct {
  uint32_t magic, today, total;
} Save;

static void load(const CpApi* api) {
  Save s;
  if (api->file_read("pomo.bin", &s, sizeof(s)) == (int)sizeof(s) && s.magic == 0x4F4D4F50u) {
    g_today = s.today;
    g_total = s.total;
  }
}
static void save(const CpApi* api) {
  Save s = {0x4F4D4F50u, g_today, g_total};
  api->file_write("pomo.bin", &s, sizeof(s));
}

static int phase_minutes(void) {
  if (g_phase == PHASE_FOCUS) return kSched[g_sched].focus;
  if (g_phase == PHASE_LONG) return kSched[g_sched].longBrk;
  return kSched[g_sched].brk;
}

static void start_phase(const CpApi* api, int phase) {
  g_phase = phase;
  g_remain_ms = (uint32_t)phase_minutes() * 60u * 1000u;
  g_end_ms = api->millis() + g_remain_ms;
  g_running = 1;
}

static void advance(const CpApi* api) {
  if (g_phase == PHASE_FOCUS) {
    ++g_today;
    ++g_total;
    save(api);
    ++g_round;
    if (g_round >= 4) {
      g_round = 0;
      start_phase(api, PHASE_LONG);
    } else {
      start_phase(api, PHASE_BREAK);
    }
  } else {
    start_phase(api, PHASE_FOCUS);
  }
}

static int32_t on_enter(const CpApi* api) {
  g_sched = 0;
  g_phase = PHASE_FOCUS;
  g_round = 0;
  g_running = 0;
  g_remain_ms = (uint32_t)kSched[0].focus * 60u * 1000u;
  load(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;

  uint32_t flags = CP_LOOP_IDLE;
  if (in->released & CP_BTN_CONFIRM) {
    if (g_running) {  // pause
      g_remain_ms = (g_end_ms > api->millis()) ? g_end_ms - api->millis() : 0;
      g_running = 0;
    } else {  // resume/start
      g_end_ms = api->millis() + g_remain_ms;
      g_running = 1;
    }
    flags = CP_LOOP_RENDER;
  }
  if ((in->released & CP_BTN_UP) && !g_running) {  // switch schedule when idle
    g_sched = (g_sched + 1) % 3;
    g_remain_ms = (uint32_t)phase_minutes() * 60u * 1000u;
    flags = CP_LOOP_RENDER;
  }
  if (in->released & CP_BTN_DOWN) {  // skip current phase
    advance(api);
    flags = CP_LOOP_RENDER;
  }
  if (g_running) {
    if (api->millis() >= g_end_ms) {
      advance(api);
      flags = CP_LOOP_RENDER;
    } else {
      // repaint about once a second for the countdown
      static uint32_t last;
      if (api->millis() - last >= 1000) {
        last = api->millis();
        flags = CP_LOOP_RENDER;
      } else {
        api->delay_ms(60);
      }
    }
  } else {
    api->delay_ms(60);
  }
  return flags;
}

static void on_render(const CpApi* api) {
  char buf[48];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  cp_snprintf(buf, sizeof(buf), "%s", kSched[g_sched].name);
  app_header(api, "番茄钟", buf);

  const char* phaseName = g_phase == PHASE_FOCUS ? "专注" : (g_phase == PHASE_LONG ? "长休息" : "休息");
  api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, 70, phaseName, 1, CP_TEXT_BOLD);

  const uint32_t remain = g_running ? ((g_end_ms > api->millis()) ? g_end_ms - api->millis() : 0) : g_remain_ms;
  const uint32_t sec = remain / 1000u;
  cp_snprintf(buf, sizeof(buf), "%02u:%02u", sec / 60u, sec % 60u);
  // Big countdown, drawn as title font scaled by eye (use TITLE font).
  api->draw_text_centered(CP_FONT_TITLE, w / 2, h / 2 - 30, buf, 1, CP_TEXT_BOLD);

  // Round pips.
  const int pipY = h / 2 + 40;
  for (int i = 0; i < 4; ++i) {
    const int px = w / 2 - 30 + i * 20;
    if (i < g_round) api->fill_rect(px, pipY, 12, 12, 1);
    else api->draw_rect(px, pipY, 12, 12, 1);
  }

  cp_snprintf(buf, sizeof(buf), "今日 %u · 累计 %u", g_today, g_total);
  api->draw_text_centered(CP_FONT_UI, w / 2, h - 70, buf, 1, CP_TEXT_REGULAR);

  app_hints(api, "返回", g_running ? "暂停" : "开始", "上=方案", "下=跳过");
  if (g_about.open) app_about_draw(api, "番茄钟");
}

static void on_exit(const CpApi* api) {
  if (g_today || g_total) save(api);
}

static const CpApp kApp = {CP_ABI_VERSION, 0, "番茄钟", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
