#pragma once

// libapp — thin C helpers shared by CrossPoint dynamic apps, layered on the
// CpApi. Keeps each app's main.c focused on its own logic: a vertical menu, a
// standard About box (author/copyright), a bottom hint bar, and small draw
// conveniences. Link libapp/app.c into the app (build-eapp.sh does this
// automatically for every app).

#include "crosspoint_app_abi.h"
#include "mini_libc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Standard chrome geometry (logical px). Apps may ignore these.
#define APP_HEADER_H 40
#define APP_FOOTER_H 30

// Fill the top bar and draw a title (white on black) plus an optional right
// status string. Returns the y just below the bar.
int app_header(const CpApi* api, const char* title, const char* right);

// Bottom hint strip: up to four short labels laid out across the width.
void app_hints(const CpApi* api, const char* b1, const char* b2, const char* b3, const char* b4);

// A vertical menu the app drives itself. Call app_menu_draw() from on_render
// and app_menu_input() from on_loop; the selection index lives in *sel.
// Returns 1 from app_menu_input() when Confirm was pressed on the selection.
void app_menu_draw(const CpApi* api, int top_y, const char* const* items, int count, int sel);
int app_menu_input(const CpApi* api, const CpInput* in, int* sel, int count);

// Standard About overlay (author / contact / location / rights / version).
// Draw it when open; toggle via app_about_input(). AppAbout state is one int.
typedef struct {
  int open;
} AppAbout;
void app_about_draw(const CpApi* api, const char* app_title);
// Returns 1 while the About overlay owns input (opened or being dismissed).
// Opens on a long Back hold when `allow_hold` is set; any key/tap closes it.
int app_about_input(const CpApi* api, const CpInput* in, AppAbout* about, int allow_hold, int* repaint);

// A scrolling single-column list, for the many app screens that outgrow
// app_menu_draw's fixed 8 rows. app_list_fit() computes how many rows the
// remaining screen height holds; draw and input keep `sel` visible.
typedef struct {
  int sel, top;        // selection, and the first visible row
  int y, row_h, rows;  // geometry, filled by app_list_fit()
} AppList;

void app_list_fit(const CpApi* api, AppList* l, int top_y, int row_h);
void app_list_draw(const CpApi* api, AppList* l, const char* const* items, int count);
// Same, for lists whose rows are not an array of pointers (packed buffers,
// generated labels). `row` is called once per visible row only.
typedef const char* (*AppRowFn)(int index, void* ctx);
void app_list_draw_fn(const CpApi* api, AppList* l, AppRowFn row, void* ctx, int count);
// Up/Down move, PageBack/PageForward jump a screen. 1 when Confirm fired.
int app_list_input(const CpApi* api, const CpInput* in, AppList* l, int count);

// Convenience: clamp helper and a centered multi-line message screen.
int app_clampi(int v, int lo, int hi);
void app_message(const CpApi* api, const char* line);

#ifdef __cplusplus
}
#endif

// --- Minimal text/JSON scanning helpers (for network apps) ---------------
// Find `"key":` and parse the integer part x1000 that follows (handles a
// leading number with up to 3 decimals). Returns 1 on success, value in *out.
int app_json_int1000(const char* buf, const char* key, long* out);
// Find `"key":"..."` and copy the string value (unescaped minimally) to out.
int app_json_str(const char* buf, const char* key, char* out, int cap);
// Position of the Nth (0-based) number inside the array that follows `key`:[.
// Returns pointer past `key`:[ for iterating, or NULL.
const char* app_find(const char* buf, const char* needle);
