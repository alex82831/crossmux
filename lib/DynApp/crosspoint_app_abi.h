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
} CpApi;

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
