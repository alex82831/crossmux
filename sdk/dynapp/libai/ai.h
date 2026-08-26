#pragma once

// libai — the client every CrossPoint AI app is built on.
//
// A local model is out of the question here: 380KB of RAM total, no PSRAM.
// So these apps talk to an OpenAI-compatible /chat/completions endpoint,
// which is the one shape every provider speaks — DeepSeek, 智谱, 月之暗面,
// 通义千问, OpenAI itself, and a llama.cpp or Ollama server on the LAN. The
// user picks; nothing here is tied to a vendor.
//
// Two device realities shape this API:
//
//   * Typing on an e-reader hurts. Credentials therefore live in the host's
//     *shared* store (ai_url / ai_key / ai_model), so a key typed once with
//     the on-screen keyboard works in every AI app afterwards.
//
//   * An app cannot block: on_loop owns the frame. Both the HTTP call and
//     text entry are request-then-poll state machines (AiJob, AiInput) that
//     let the app paint "thinking" *before* it stalls for 20-40 seconds.
//
// Link by including "ai.h"; build-eapp.sh then pulls libai/ai.c in.

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Configuration -----------------------------------------------------

#define AI_URL_LEN 160
#define AI_KEY_LEN 132
#define AI_MODEL_LEN 64
#define AI_LANG_LEN 24

typedef struct {
  char base_url[AI_URL_LEN];  // e.g. https://api.deepseek.com/v1
  char api_key[AI_KEY_LEN];
  char model[AI_MODEL_LEN];
  char lang[AI_LANG_LEN];  // answer language, e.g. 中文
} AiConfig;

// Read the shared keys, filling in defaults for anything unset.
void ai_config_load(const CpApi* api, AiConfig* cfg);
// Persist all four keys. Returns 1 when every write landed.
int ai_config_save(const CpApi* api, const AiConfig* cfg);
// 1 when a call can actually be attempted (url + key + model present).
int ai_config_ready(const AiConfig* cfg);
// The URL that will really be POSTed to — worth showing in a settings screen,
// because "why do I get 404" is almost always a base_url that lost its /v1.
void ai_endpoint(const AiConfig* cfg, char* out, int cap);

// ---- One call ----------------------------------------------------------

typedef struct {
  const char* role;  // "user" or "assistant"
  const char* text;
} AiTurn;

typedef enum {
  AI_IDLE = 0,
  AI_ARMED,  // queued; the HTTP call happens on the next pump
  AI_DONE,
  AI_FAILED,
} AiState;

typedef struct {
  AiState state;
  const char* note;    // one line under the spinner ("正在解释这个词…")
  const char* result;  // the answer, or a human-readable failure
  uint32_t started_ms;
  uint32_t took_ms;
  int swallow;  // 1 for the frame right after a call, see ai_job_settled()
} AiJob;

// Queue a call. Return CP_LOOP_RENDER right after this so the busy screen is
// on the panel before ai_job_pump() blocks.
void ai_job_start(AiJob* job, const char* note);

// Runs the queued call. Blocks for as long as the model takes (safe: a
// blocking socket read yields, and nothing is subscribed to the watchdog).
// Returns 1 if a call ran this frame. `sys` may be NULL.
int ai_job_pump(const CpApi* api, AiJob* job, const AiConfig* cfg, const char* sys, const AiTurn* turns, int count,
                int max_tokens);

// Buttons pressed *during* the call surface on the frame after it. Call this
// first in on_loop and bail when it returns 1, so a stray press cannot, say,
// exit the app the instant the answer lands.
int ai_job_settled(AiJob* job);

// Full-screen "thinking" panel, identical across every AI app.
void ai_job_draw_busy(const CpApi* api, const AiJob* job, const char* title);

// ---- Text entry --------------------------------------------------------

#define AI_INPUT_MAX 512

typedef struct {
  int pending;
  char buf[AI_INPUT_MAX];
} AiInput;

// 1 when the host accepted the request. `initial` may be NULL.
int ai_input_begin(const CpApi* api, AiInput* inp, const char* title, const char* initial, int max_len);
// 1 text ready in inp->buf, 0 still typing, -1 cancelled, -2 nothing pending.
int ai_input_poll(const CpApi* api, AiInput* inp);

// ---- Paged long text (e-ink) -------------------------------------------
// Answers are long and the panel is slow, so text is laid out once into line
// offsets and then paged — no reflow per frame, no partial redraws.

#define AI_PAGER_MAX_LINES 400

typedef struct {
  const char* text;
  int font, style;
  int x, y, w;
  int line_h;
  int lines_per_page;
  int line_count;
  int page;
  unsigned short starts[AI_PAGER_MAX_LINES + 1];
} AiPager;

// Lay `text` out into the box. Keeps the pointer, so `text` must outlive use.
void ai_pager_set(const CpApi* api, AiPager* p, const char* text, int font, int x, int y, int w, int h);
void ai_pager_draw(const CpApi* api, const AiPager* p);
// Up/Down/PageBack/PageForward/Left/Right turn pages. 1 when the page moved.
int ai_pager_input(const CpApi* api, const CpInput* in, AiPager* p);
int ai_pager_pages(const AiPager* p);
// 1 when the text is longer than one page (worth drawing "1/3").
int ai_pager_scrollable(const AiPager* p);

// The request buffer, lent out between calls. Valid only when no call is in
// flight (i.e. not between ai_job_start and ai_job_pump). Saves every app
// from carrying a second multi-KB scratch just to rewrite a history file.
char* ai_scratch(int* cap);

// ---- Small shared utilities --------------------------------------------

// Copy at most cap-1 bytes, never splitting a UTF-8 sequence at the end.
void ai_str_copy(char* out, int cap, const char* src);
// Append, same UTF-8 safety. Returns the resulting length.
int ai_str_append(char* out, int cap, const char* src);
// Longest prefix of `src` that fits in `max_bytes` without splitting a
// character; writes it to `out`. Used to send a passage without truncating
// mid-glyph.
void ai_str_clip(char* out, int cap, const char* src, int max_bytes);
// Strip a leading UTF-8 BOM and normalise CRLF in place. Returns new length.
// Deliberately keeps a trailing newline: for a line-structured file it is a
// record terminator, not noise.
int ai_text_normalize(char* s);
// Drop trailing whitespace. For single-line values read with file_read, where
// an editor's stray newline would otherwise end up inside a path or a key.
void ai_str_trim_end(char* s);

#ifdef __cplusplus
}
#endif
