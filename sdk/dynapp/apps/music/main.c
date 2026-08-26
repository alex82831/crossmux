// 音乐播放器 — plays music off the SD card through a DLNA renderer on the LAN.
//
// These boards have no audio hardware, and the ESP32-C3 has only Bluetooth LE
// (no Classic/BR-EDR), so neither on-device playback nor A2DP headphones are
// possible. The workable route is the classic three-box DLNA model: this app
// is the controller, the firmware publishes the track over HTTP off the SD
// card, and a renderer on the LAN (speaker, TV, phone app) pulls and plays it.

#include "app.h"
#include "dlna.h"

#define MAX_TRACKS 64
#define NAME_LEN 64
#define MUSIC_DIR "/music"

enum { V_LIST, V_RENDERERS, V_PLAYER };

static char g_tracks[MAX_TRACKS][NAME_LEN];
static int g_trackCount;
static int g_sel;          // cursor in the track list
static int g_playing = -1; // index of the track handed to the renderer

static DlnaRenderer g_renderers[DLNA_MAX_RENDERERS];
static int g_rendererCount;
static int g_renderer = -1;  // chosen renderer
static int g_rendSel;

static int g_view = V_LIST;
static int g_volume = 40;
static int g_paused;
static int g_elapsed = -1, g_total = -1;
static uint32_t g_lastPoll;
static char g_status[64];
static AppAbout g_about;

typedef struct {
  uint32_t magic;
  int volume;
  char rendererName[DLNA_NAME_LEN];
} Prefs;
#define PREFS_MAGIC 0x3153554Du  // "MUS1"

static void set_status(const char* s) { cp_snprintf(g_status, sizeof(g_status), "%s", s); }

static int has_music_ext(const char* name) {
  const int n = (int)strlen(name);
  static const char* exts[] = {".mp3", ".m4a", ".aac", ".wav", ".flac", ".ogg", ".opus", ".wma", ".m4b"};
  for (unsigned e = 0; e < sizeof(exts) / sizeof(exts[0]); ++e) {
    const int el = (int)strlen(exts[e]);
    if (n <= el) continue;
    int match = 1;
    for (int i = 0; i < el; ++i) {
      char a = name[n - el + i], b = exts[e][i];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (a != b) { match = 0; break; }
    }
    if (match) return 1;
  }
  return 0;
}

// ---- library -------------------------------------------------------------

static void load_tracks(const CpApi* api) {
  g_trackCount = 0;
  static char listing[4096];
  const int n = api->dir_list(MUSIC_DIR, listing, sizeof(listing));
  if (n <= 0) return;
  listing[n] = 0;

  // Records are "name\tsize\tD|F\n".
  const char* p = listing;
  while (*p && g_trackCount < MAX_TRACKS) {
    const char* tab = p;
    while (*tab && *tab != '\t') ++tab;
    if (!*tab) break;
    const int nameLen = (int)(tab - p);
    // Skip to the type field to filter out directories.
    const char* second = tab + 1;
    while (*second && *second != '\t') ++second;
    const char kind = second[1] ? second[1] : 'F';

    if (kind == 'F' && nameLen > 0 && nameLen < NAME_LEN) {
      char name[NAME_LEN];
      for (int i = 0; i < nameLen; ++i) name[i] = p[i];
      name[nameLen] = 0;
      if (has_music_ext(name)) {
        cp_snprintf(g_tracks[g_trackCount], NAME_LEN, "%s", name);
        ++g_trackCount;
      }
    }
    while (*p && *p != '\n') ++p;
    if (*p == '\n') ++p;
  }
}

static void save_prefs(const CpApi* api) {
  Prefs p;
  memset(&p, 0, sizeof(p));
  p.magic = PREFS_MAGIC;
  p.volume = g_volume;
  if (g_renderer >= 0) cp_snprintf(p.rendererName, sizeof(p.rendererName), "%s", g_renderers[g_renderer].name);
  api->file_write("music.bin", &p, sizeof(p));
}

static void load_prefs(const CpApi* api, char* wantName, int cap) {
  Prefs p;
  wantName[0] = 0;
  if (api->file_read("music.bin", &p, sizeof(p)) != (int)sizeof(p) || p.magic != PREFS_MAGIC) return;
  if (p.volume >= 0 && p.volume <= 100) g_volume = p.volume;
  cp_snprintf(wantName, cap, "%s", p.rendererName);
}

// ---- playback ------------------------------------------------------------

static int start_track(const CpApi* api, int index) {
  if (index < 0 || index >= g_trackCount || g_renderer < 0) return 0;
  char abs[128];
  cp_snprintf(abs, sizeof(abs), "%s/%s", MUSIC_DIR, g_tracks[index]);

  char url[320];
  if (!api->media_publish(abs, url, sizeof(url))) {
    set_status("无法发布该文件");
    return 0;
  }
  const DlnaRenderer* r = &g_renderers[g_renderer];
  if (!dlna_set_uri(api, r, url, g_tracks[index])) {
    set_status("播放设备拒绝了该地址");
    return 0;
  }
  if (!dlna_play(api, r)) {
    set_status("播放指令失败");
    return 0;
  }
  dlna_set_volume(api, r, g_volume);
  g_playing = index;
  g_paused = 0;
  g_elapsed = g_total = -1;
  set_status("");
  return 1;
}

static void poll_position(const CpApi* api) {
  if (g_renderer < 0 || g_playing < 0) return;
  dlna_position(api, &g_renderers[g_renderer], &g_elapsed, &g_total);
}

static void discover(const CpApi* api, const char* preferName) {
  app_message(api, "正在搜索播放设备…");
  g_rendererCount = dlna_discover(api, g_renderers, DLNA_MAX_RENDERERS, 3000);
  g_renderer = -1;
  if (g_rendererCount <= 0) {
    set_status("未发现 DLNA 设备");
    return;
  }
  if (preferName && preferName[0]) {  // reuse last time's choice when present
    for (int i = 0; i < g_rendererCount; ++i) {
      if (strcmp(g_renderers[i].name, preferName) == 0) {
        g_renderer = i;
        break;
      }
    }
  }
  if (g_renderer < 0 && g_rendererCount == 1) g_renderer = 0;
  set_status("");
}

// ---- lifecycle -----------------------------------------------------------

static int32_t on_enter(const CpApi* api) {
  // The media-control entries are appended to CpApi; refuse politely on a
  // firmware that predates them rather than calling through a null pointer.
  if (api->size < sizeof(CpApi)) return -1;
  g_view = V_LIST;
  g_sel = 0;
  g_playing = -1;
  g_renderer = -1;
  g_rendererCount = 0;
  g_status[0] = 0;
  load_tracks(api);
  return 0;
}

static uint32_t on_loop(const CpApi* api, const CpInput* in) {
  int repaint = 0;
  if (app_about_input(api, in, &g_about, 1, &repaint)) return repaint ? CP_LOOP_RENDER : CP_LOOP_IDLE;

  // ---- renderer picker ----
  if (g_view == V_RENDERERS) {
    if (in->released & CP_BTN_BACK) {
      g_view = V_LIST;
      return CP_LOOP_RENDER;
    }
    if (g_rendererCount > 0) {
      if (in->released & CP_BTN_UP) {
        g_rendSel = (g_rendSel + g_rendererCount - 1) % g_rendererCount;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_DOWN) {
        g_rendSel = (g_rendSel + 1) % g_rendererCount;
        return CP_LOOP_RENDER;
      }
      if (in->released & CP_BTN_CONFIRM) {
        g_renderer = g_rendSel;
        save_prefs(api);
        g_view = V_LIST;
        return CP_LOOP_RENDER;
      }
    }
    if (in->released & CP_BTN_RIGHT) {  // re-scan
      discover(api, 0);
      g_rendSel = 0;
      return CP_LOOP_RENDER;
    }
    api->delay_ms(50);
    return CP_LOOP_IDLE;
  }

  // ---- now playing ----
  if (g_view == V_PLAYER) {
    if (in->released & CP_BTN_BACK) {
      g_view = V_LIST;
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_CONFIRM) {
      if (g_renderer >= 0) {
        if (g_paused) {
          dlna_play(api, &g_renderers[g_renderer]);
          g_paused = 0;
        } else {
          dlna_pause(api, &g_renderers[g_renderer]);
          g_paused = 1;
        }
      }
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_LEFT) {  // previous
      if (g_playing > 0) start_track(api, g_playing - 1);
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_RIGHT) {  // next
      if (g_playing >= 0 && g_playing + 1 < g_trackCount) start_track(api, g_playing + 1);
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_UP) {
      g_volume += 5;
      if (g_volume > 100) g_volume = 100;
      if (g_renderer >= 0) dlna_set_volume(api, &g_renderers[g_renderer], g_volume);
      save_prefs(api);
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_DOWN) {
      g_volume -= 5;
      if (g_volume < 0) g_volume = 0;
      if (g_renderer >= 0) dlna_set_volume(api, &g_renderers[g_renderer], g_volume);
      save_prefs(api);
      return CP_LOOP_RENDER;
    }
    // Poll position about every 5s; e-ink makes anything faster pointless.
    if (!g_paused && api->millis() - g_lastPoll > 5000) {
      g_lastPoll = api->millis();
      poll_position(api);
      // Auto-advance when the renderer reports the track finished.
      if (g_total > 0 && g_elapsed >= 0 && g_elapsed >= g_total - 1 && g_playing + 1 < g_trackCount) {
        start_track(api, g_playing + 1);
      }
      return CP_LOOP_RENDER;
    }
    api->delay_ms(60);
    return CP_LOOP_IDLE;
  }

  // ---- track list ----
  if (in->released & CP_BTN_BACK) return CP_LOOP_EXIT;
  if (g_trackCount > 0) {
    if (in->released & CP_BTN_UP) {
      g_sel = (g_sel + g_trackCount - 1) % g_trackCount;
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_DOWN) {
      g_sel = (g_sel + 1) % g_trackCount;
      return CP_LOOP_RENDER;
    }
    if (in->released & CP_BTN_CONFIRM) {
      if (g_renderer < 0) {  // pick a renderer before the first play
        char want[DLNA_NAME_LEN];
        load_prefs(api, want, sizeof(want));
        discover(api, want);
        if (g_renderer < 0) {
          g_rendSel = 0;
          g_view = V_RENDERERS;
          return CP_LOOP_RENDER;
        }
      }
      app_message(api, "正在投送…");
      if (start_track(api, g_sel)) g_view = V_PLAYER;
      return CP_LOOP_RENDER;
    }
  }
  if (in->released & CP_BTN_RIGHT) {  // change/scan renderers
    char want[DLNA_NAME_LEN];
    load_prefs(api, want, sizeof(want));
    discover(api, want);
    g_rendSel = 0;
    g_view = V_RENDERERS;
    return CP_LOOP_RENDER;
  }
  if ((in->released & CP_BTN_LEFT) && g_playing >= 0) {  // back to now-playing
    g_view = V_PLAYER;
    return CP_LOOP_RENDER;
  }
  api->delay_ms(50);
  return CP_LOOP_IDLE;
}

// ---- rendering -----------------------------------------------------------

static void draw_time(const CpApi* api, int y) {
  char buf[48];
  if (g_elapsed < 0) {
    api->draw_text_centered(CP_FONT_UI, api->screen_width() / 2, y, "--:-- / --:--", 1, CP_TEXT_REGULAR);
    return;
  }
  if (g_total > 0) {
    cp_snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d", g_elapsed / 60, g_elapsed % 60, g_total / 60, g_total % 60);
  } else {
    cp_snprintf(buf, sizeof(buf), "%d:%02d", g_elapsed / 60, g_elapsed % 60);
  }
  api->draw_text_centered(CP_FONT_UI, api->screen_width() / 2, y, buf, 1, CP_TEXT_REGULAR);
}

static void on_render(const CpApi* api) {
  char buf[80];
  const int w = api->screen_width();
  const int h = api->screen_height();
  api->clear_screen();

  if (g_view == V_RENDERERS) {
    app_header(api, "选择播放设备", "");
    if (g_rendererCount <= 0) {
      api->draw_text_centered(CP_FONT_UI, w / 2, h / 2 - 20, "未发现 DLNA 播放设备", 1, CP_TEXT_BOLD);
      api->draw_text_wrapped(CP_FONT_UI, 24, h / 2 + 10, w - 48, 3,
                             "请确认音箱/电视已开启 DLNA（有的叫“投屏”“DMR”），且与本机在同一 Wi-Fi 下", 1,
                             CP_TEXT_REGULAR);
    } else {
      int y = 60;
      for (int i = 0; i < g_rendererCount; ++i) {
        const int sel = (i == g_rendSel);
        if (sel) api->fill_rect(14, y - 4, w - 28, 36, 1);
        api->draw_text(CP_FONT_UI_LARGE, 22, y, g_renderers[i].name, sel ? 0 : 1, CP_TEXT_REGULAR);
        y += 42;
      }
    }
    app_hints(api, "返回", "选定", "上下选择", "右=重新搜索");
    if (g_about.open) app_about_draw(api, "音乐播放器");
    return;
  }

  if (g_view == V_PLAYER) {
    cp_snprintf(buf, sizeof(buf), "%s", g_renderer >= 0 ? g_renderers[g_renderer].name : "");
    app_header(api, g_paused ? "已暂停" : "正在播放", buf);

    if (g_playing >= 0) {
      api->draw_text_wrapped(CP_FONT_UI_LARGE, 20, 70, w - 40, 3, g_tracks[g_playing], 1, CP_TEXT_BOLD);
    }
    draw_time(api, h / 2);

    // Volume bar.
    const int barW = w - 80, barX = 40, barY = h / 2 + 44;
    api->draw_rect(barX, barY, barW, 18, 1);
    api->fill_rect(barX + 2, barY + 2, (barW - 4) * g_volume / 100, 14, 1);
    cp_snprintf(buf, sizeof(buf), "音量 %d%%", g_volume);
    api->draw_text_centered(CP_FONT_UI, w / 2, barY + 26, buf, 1, CP_TEXT_REGULAR);

    if (g_status[0]) api->draw_text_centered(CP_FONT_SMALL, w / 2, h - 66, g_status, 1, CP_TEXT_REGULAR);
    app_hints(api, "返回", g_paused ? "继续" : "暂停", "左右切歌", "上下音量");
    if (g_about.open) app_about_draw(api, "音乐播放器");
    return;
  }

  // Track list.
  cp_snprintf(buf, sizeof(buf), "%s", g_renderer >= 0 ? g_renderers[g_renderer].name : "未选设备");
  app_header(api, "音乐播放器", buf);

  if (g_trackCount == 0) {
    api->draw_text_centered(CP_FONT_UI_LARGE, w / 2, h / 2 - 30, "没有找到音乐", 1, CP_TEXT_BOLD);
    api->draw_text_wrapped(CP_FONT_UI, 24, h / 2 + 4, w - 48, 3,
                           "请把 mp3/m4a/flac 等音乐文件放到 SD 卡的 /music 目录下", 1, CP_TEXT_REGULAR);
  } else {
    int first = g_sel - 4;
    if (first < 0) first = 0;
    int y = 58;
    for (int i = first; i < g_trackCount && y < h - 70; ++i) {
      const int sel = (i == g_sel);
      if (sel) api->fill_rect(12, y - 4, w - 24, 34, 1);
      const int nowPlaying = (i == g_playing);
      cp_snprintf(buf, sizeof(buf), "%s%s", nowPlaying ? "▶ " : "", g_tracks[i]);
      api->draw_text(CP_FONT_UI, 20, y, buf, sel ? 0 : 1, nowPlaying ? CP_TEXT_BOLD : CP_TEXT_REGULAR);
      y += 38;
    }
  }
  if (g_status[0]) api->draw_text_centered(CP_FONT_SMALL, w / 2, h - 62, g_status, 1, CP_TEXT_REGULAR);
  app_hints(api, "返回", "播放", "右=选设备", g_playing >= 0 ? "左=正在播放" : "");
  if (g_about.open) app_about_draw(api, "音乐播放器");
}

static void on_exit(const CpApi* api) {
  // Leave the renderer stopped: the firmware drops the published URL when the
  // app unloads, and a renderer left playing a dead URL just stalls.
  if (g_renderer >= 0 && g_playing >= 0) dlna_stop(api, &g_renderers[g_renderer]);
}

static const CpApp kApp = {CP_ABI_VERSION, 0, "音乐播放器", 10100, on_enter, on_loop, on_render, on_exit};
__attribute__((visibility("default"))) const CpApp* cp_app_entry(void) { return &kApp; }
