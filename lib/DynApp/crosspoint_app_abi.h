#pragma once

// CrossPoint dynamic-app ABI — the single contract shared by the firmware
// loader (lib/DynApp) and the app SDK (sdk/dynapp). Pure C so the app side
// can be built with -nostdlib and no C++ runtime.
//
// An installable app (`.eapp`) is a position-independent ELF32 RISC-V shared
// object built by sdk/dynapp/build-eapp.sh. The firmware loads it into a
// single heap block, applies R_RISCV_RELATIVE relocations, and calls the
// entry point, which returns a `CpApp` callback table. The app talks back to
// the firmware exclusively through the `CpApi` function table passed to every
// callback — it imports no symbols at all.
//
// Versioning: CP_ABI_VERSION only bumps on breaking layout changes. New
// functions are appended to the END of CpApi (never reordered), so an old app
// keeps working on a newer firmware; an app that needs newer entries checks
// `api_min` at load time. Document format details in docs/file-formats.md.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP_ABI_VERSION 1u

// Entry point every .eapp exports: `const CpApp* cp_app_entry(void);`
#define CP_APP_ENTRY_NAME "cp_app_entry"

// ---- Input ------------------------------------------------------------
// Logical buttons, mirroring MappedInputManager::Button. Delivered as
// bitmasks; the firmware guarantees one gesture = one released bit.
enum {
  CP_BTN_BACK = 1u << 0,
  CP_BTN_CONFIRM = 1u << 1,
  CP_BTN_LEFT = 1u << 2,
  CP_BTN_RIGHT = 1u << 3,
  CP_BTN_UP = 1u << 4,
  CP_BTN_DOWN = 1u << 5,
  CP_BTN_PAGE_BACK = 1u << 6,
  CP_BTN_PAGE_FORWARD = 1u << 7,
};

typedef struct {
  uint32_t released;  // buttons released this frame (act on these)
  uint32_t held;      // buttons currently held
  int32_t touch_x;    // last tap position, -1 when no tap this frame
  int32_t touch_y;
  uint8_t tapped;  // 1 when a tap was released this frame
} CpInput;

// ---- Fonts ------------------------------------------------------------
// Stable font handles resolved by the firmware to real font ids. Small/UI
// sizes carry the full built-in glyph set of the running firmware (incl. CJK
// on the Chinese build).
enum {
  CP_FONT_SMALL = 0,  // status/footnote size
  CP_FONT_UI = 1,     // default UI size
  CP_FONT_UI_LARGE = 2,
  CP_FONT_TITLE = 3,  // large headline
};

enum {
  CP_TEXT_REGULAR = 0,
  CP_TEXT_BOLD = 1,
};

// ---- App loop ---------------------------------------------------------
// Flags returned by on_loop().
enum {
  CP_LOOP_IDLE = 0,
  CP_LOOP_RENDER = 1u << 0,  // request on_render + display
  CP_LOOP_EXIT = 1u << 1,    // leave the app (firmware frees everything)
};

// ---- Firmware services ------------------------------------------------
// All coordinates are logical (current orientation); origin top-left.
// `black != 0` draws ink, `black == 0` draws white/erase.
typedef struct CpApi {
  uint32_t abi_version;  // CP_ABI_VERSION of the running firmware
  uint32_t size;         // sizeof(CpApi): apps may probe for appended entries

  // System
  uint32_t (*millis)(void);
  void (*delay_ms)(uint32_t ms);
  uint32_t (*random_u32)(void);
  void (*log)(const char* tag, const char* msg);
  int32_t (*battery_percent)(void);
  uint32_t (*free_heap)(void);

  // Screen
  int32_t (*screen_width)(void);
  int32_t (*screen_height)(void);
  void (*clear_screen)(void);  // all white

  // Drawing (into the shared framebuffer; shown after on_render returns)
  void (*draw_pixel)(int32_t x, int32_t y, int32_t black);
  void (*draw_line)(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t black);
  void (*draw_rect)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t black);
  void (*fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t black);
  void (*draw_text)(int32_t font, int32_t x, int32_t y, const char* utf8, int32_t black, int32_t style);
  int32_t (*text_width)(int32_t font, const char* utf8, int32_t style);
  int32_t (*line_height)(int32_t font);

  // Storage, sandboxed to this app's data directory (/apps/data/<slug>/).
  // Paths are relative ("scores.bin"); ".." is rejected. Reads return bytes
  // read (<0 on error); writes replace the file atomically enough for our
  // use (write then close) and return 0 on success.
  int32_t (*file_read)(const char* rel_path, void* buf, uint32_t capacity);
  int32_t (*file_write)(const char* rel_path, const void* data, uint32_t len);
  int32_t (*file_delete)(const char* rel_path);
  int32_t (*file_exists)(const char* rel_path);

  // ---- Appended in ABI v1, size-probed (check api->size) ----
  // Text convenience: center on `cx`, or wrap within `width` for `max_lines`.
  void (*draw_text_centered)(int32_t font, int32_t cx, int32_t y, const char* utf8, int32_t black, int32_t style);
  void (*draw_text_wrapped)(int32_t font, int32_t x, int32_t y, int32_t width, int32_t max_lines, const char* utf8,
                            int32_t black, int32_t style);

  // Local wall clock (from the device RTC). Any out-pointer may be NULL.
  // Returns 1 on success, 0 if the clock is unset.
  int32_t (*rtc_now)(int32_t* year, int32_t* month, int32_t* day, int32_t* hour, int32_t* minute, int32_t* second,
                     int32_t* weekday);

  // Networking. wifi_connected() is a status check. wifi_ensure() brings the
  // STA link up by connecting to a saved network (last-connected first),
  // blocking up to timeout_ms; returns 1 if connected. http_get() does an
  // HTTPS GET into `buf` (encrypted, server cert NOT verified — same posture
  // as the firmware's other fetches), returning bytes read (< buf), or a
  // negative CpHttpError. The host powers Wi-Fi down when the app exits.
  int32_t (*wifi_connected)(void);
  int32_t (*wifi_ensure)(uint32_t timeout_ms);
  int32_t (*http_get)(const char* url, void* buf, uint32_t capacity);

  // ---- Appended again, size-probed: LAN media control ----
  // Enough to be a DLNA/UPnP control point. The device cannot play audio
  // itself (no codec on these boards, and the C3 has no Bluetooth Classic so
  // no A2DP either) — it discovers a renderer on the LAN, serves the track,
  // and drives playback.

  // Read-only listing of an absolute SD path (outside the app's sandbox, so
  // a player can browse /music). Writes newline-separated records into `buf`:
  //     name \t size \t D|F
  // Returns bytes written (excluding the terminator), or negative on error.
  int32_t (*dir_list)(const char* abs_path, char* buf, uint32_t capacity);

  // SSDP M-SEARCH on 239.255.255.250:1900. Collects responses until
  // timeout_ms elapses and writes them back-to-back into `buf`. Returns bytes
  // written, or negative on error. Parse the LOCATION headers yourself.
  int32_t (*ssdp_discover)(const char* search_target, uint32_t timeout_ms, char* buf, uint32_t capacity);

  // HTTP POST with an explicit content type and one optional extra header
  // ("SOAPAction: ..." for UPnP, "Authorization: Bearer ..." for an API).
  // Returns bytes of the response body, or a negative CpHttpError; on
  // CP_HTTP_ERR_STATUS and CP_HTTP_ERR_OVERFLOW the body is still in `buf`.
  //
  // https works, but — exactly like http_get and every other fetch this
  // firmware makes — it is encrypted with the server UNVERIFIED: the wolfSSL
  // transport has no CA bundle wired up, and linking mbedTLS alongside it to
  // get one costs ~193KB of flash. Treat anything sent through here as
  // readable by an active man-in-the-middle on a hostile network.
  int32_t (*http_post)(const char* url, const char* content_type, const char* extra_header, const char* body, void* buf,
                       uint32_t capacity);

  // Publish one media file on the LAN and write the URL a renderer should
  // fetch into `url_out`. Starts the device's media listener on first use and
  // keeps the file reachable until the next call. Only audio/video types are
  // accepted. Returns 1 on success, 0 on refusal (no link, bad type, missing).
  int32_t (*media_publish)(const char* abs_path, char* url_out, uint32_t capacity);

  // ---- Appended again, size-probed: text entry, shared config, file reads ----
  // What an app needs to talk to a cloud service: a way to take typed input,
  // one place to keep credentials so every app does not ask again, and
  // read access to content already on the card.

  // Ask the host to open its on-screen keyboard. The app cannot block, so this
  // is request-then-poll: call begin() once, return CP_LOOP_IDLE, then call
  // result() each frame. The app stays loaded while the keyboard is up.
  //   begin()  -> 1 when the request was accepted
  //   result() -> 1 text ready (written to buf), 0 still open, -1 cancelled,
  //               -2 nothing was requested
  int32_t (*text_input_begin)(const char* title, const char* initial, uint32_t max_len);
  int32_t (*text_input_result)(char* buf, uint32_t capacity);

  // Cross-app key/value store under /apps/data/_shared/. Deliberately shared:
  // an API key typed once should work in every app that needs it. Keys are
  // [a-z0-9_]. get() returns bytes written (0 when unset), set() 1 on success.
  int32_t (*shared_get)(const char* key, char* buf, uint32_t capacity);
  int32_t (*shared_set)(const char* key, const char* value);

  // Read-only read of an absolute SD path, for content the user already has
  // (books, notes). `offset` allows paging through a file larger than RAM.
  // Returns bytes read, or negative on error.
  int32_t (*file_read_abs)(const char* abs_path, uint32_t offset, void* buf, uint32_t capacity);
} CpApi;

// http_get / http_post error codes (negative returns).
enum {
  CP_HTTP_ERR_ARGS = -1,
  CP_HTTP_ERR_NO_WIFI = -2,
  CP_HTTP_ERR_TRANSPORT = -3,
  CP_HTTP_ERR_OVERFLOW = -4,
  // The server answered, but not with 2xx. `buf` still holds the response
  // body, which is where an API puts the reason (bad key, no quota, ...).
  CP_HTTP_ERR_STATUS = -5,
};

// ---- App callbacks ----------------------------------------------------
typedef struct CpApp {
  uint32_t abi_version;  // must equal CP_ABI_VERSION the app was built with
  uint32_t api_min;      // minimum CpApi.size the app requires (0 = any v1)
  const char* name;      // display name (UTF-8; may be NULL -> filename)
  uint32_t version;      // app version, e.g. 10203 = 1.2.3

  // Called once after load. Return 0 to continue, nonzero to abort launch.
  int32_t (*on_enter)(const CpApi* api);
  // Called every frame with the fresh input snapshot. Return CP_LOOP_* flags.
  uint32_t (*on_loop)(const CpApi* api, const CpInput* input);
  // Called when on_loop requested CP_LOOP_RENDER; draw the full frame.
  void (*on_render)(const CpApi* api);
  // Called once before unload (also on forced exit). May be NULL.
  void (*on_exit)(const CpApi* api);
} CpApp;

typedef const CpApp* (*CpAppEntryFn)(void);

#ifdef __cplusplus
}  // extern "C"
#endif
