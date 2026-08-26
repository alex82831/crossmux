# DynApp — installable apps on the ESP32-C3

CrossMux can install, update, run and uninstall apps at runtime, without
reflashing the firmware and without touching user data. An installable app is
a single `.eapp` file on SD; the App Manager (应用管理) installs them from
the online catalog, and any file that lands in `/apps/` (web-file-manager
upload, WebDAV, manual copy) is equally an install.

**The firmware ships only the App Manager plus the pieces that are firmware
infrastructure** — File Transfer (the app-install upload channel), OPDS and
WeRead (library + reader integration), AirPage and Pixel Switch (MQTT),
Reading Stats (reader DB) and Standby (the system sleep screen). Every other
former built-in — the games, the tools, the network apps — is now an
installable `.eapp` built from [`sdk/dynapp/apps/`](../../sdk/dynapp/apps) and
served from [`store/`](../../store) (20 apps at time of writing). Their CJK
text renders through the firmware's embedded fonts, so the Chinese build keeps
its full glyph coverage even though the app code left the image.

This document is the system of record for how that works. The pieces:

| Piece | Where |
|---|---|
| ABI contract (`CpApi` / `CpApp`) | `lib/DynApp/crosspoint_app_abi.h` |
| Loader | `lib/DynApp/DynAppLoader.{h,cpp}` |
| API bridge + host activity | `src/activities/apps/appmgr/DynAppApi.*`, `DynAppActivity.*` |
| Registry + App Manager UI | `src/activities/apps/appmgr/DynAppRegistry.*`, `AppManagerActivity.*` |
| App SDK (build an .eapp) | `sdk/dynapp/` |
| Sample apps + hosted catalog | `sdk/dynapp/apps/`, `store/` |

## Why this is hard on the C3, and the trick that makes it work

The ESP32-C3 maps its single SRAM **twice**: the data bus sees it at
`0x3FC80000+`, the instruction bus at `0x40380000+` (`SOC_I_D_OFFSET` =
`0x700000` apart). Instruction fetch works only through the I window; byte
loads/stores only through the D window (`SOC_BYTE_ACCESSIBLE_*`). RISC-V
`-fPIC` code addresses its own data PC-relatively (`auipc`), so a loaded
image cannot split text and data into separately-placed blocks (that is why
Espressif's elf_loader supports the Xtensa chips and the unified-address
C6/P4, but not the C3).

The escape is a **link-time layout that pre-compensates the alias offset**:

- `.text` links at vaddr `0x700000`, and loads at physical offset `0` of one
  contiguous heap block.
- Everything byte-accessed (`.rodata`, `.data`, `.bss`, GOT, dynamic
  metadata) links at vaddr == its physical offset in the block (i.e. base
  `align16(text_size)` — the SDK links twice to learn that number).

With the block allocated at D-address `P` (I alias `P + 0x700000`), every
link-time address `A` resolves at runtime to simply **`P + A`**:

- text addresses (`A ≥ 0x700000`) land in the I window — correct for
  `e_entry`, call targets and function pointers;
- data addresses land in the D window — correct for byte access;
- `auipc` arithmetic is preserved because link-time deltas equal runtime
  deltas by construction (PC runs in the I window; `pc + (A_data − A_pc)` =
  `P + A_data`).

The loader therefore applies `R_RISCV_RELATIVE` relocations as
`*(u32*)(P + r_offset) = P + addend` and nothing else. Apps import **no
symbols**: the firmware hands them a versioned `CpApi` function table, and
`sdk/dynapp/build-eapp.sh` links with `--no-undefined` so a stray libc call
fails at build time (a mini-libc provides `mem*`/`strlen`/`cp_snprintf`).

Loading (all through the HAL, `DynAppLoader::load`):

1. Parse/validate the ELF header (ELF32 LSB, `ET_DYN`, `EM_RISCV`, bounded
   phnum/shnum) and the layout invariants above.
2. `heap_caps_aligned_alloc(16, size, MALLOC_CAP_EXEC)` — one block, image
   cap **96 KB** (`kMaxImageBytes`). Refused loads fail with a themed error
   screen, never a crash.
3. Copy `PT_LOAD` segments through the D alias, zero `.bss`.
4. Apply relocations (RELATIVE only; anything else → `BadReloc`).
5. `fence.i`, call `cp_app_entry()`, validate the returned `CpApp`
   (ABI version, callback pointers, struct inside the image).

Unload frees the one block — the whole app costs zero RAM when not running.

## The PMP trade-off

Executing from heap requires `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n`
(set in `platformio.ini` `custom_sdkconfig`): the C3's PMP otherwise marks
RAM no-execute and locks at boot, and with it off `MALLOC_CAP_EXEC` exists
at all (`components/heap/port/esp32c3/memory_layout.c`). This removes the
W^X mitigation for the whole firmware. On a single-user reading device with
no untrusted network inputs beyond what the user installs, that is an
accepted trade; it is the same posture Tasmota/Berry-class ecosystems take.
A `.eapp` runs with **full firmware privileges** — install apps you trust.
The loader's structural checks reject malformed images, not malicious ones.

## App lifecycle and API

`DynAppActivity` hosts the app inside the normal Activity model: load +
`on_enter` in `onEnter()`, `on_loop(api, input)` each frame (returns
`CP_LOOP_RENDER` / `CP_LOOP_EXIT` flags), `on_render(api)` under the render
lock, `on_exit` + unload in `onExit()`. **Holding Back ≈1.5 s always
force-exits**, so a misbehaving app cannot trap the user. Input arrives as
logical-button bitmasks plus a tap point — one gesture, one released bit.

`CpApi` v1 (see the header for the authoritative list): millis/delay/
random/log/battery/free-heap, screen size + clear, pixel/line/rect/fill/
text/text-width/line-height (font handles `CP_FONT_*` map to built-in
fonts, CJK included on the CN build), and sandboxed file I/O confined to
`/apps/data/<slug>/` (path-checked; `..` rejected). New entries are only
ever appended; `CpApi.size` lets apps probe.

## On-SD layout and the catalog

```
/apps/<slug>.eapp      the app image        (install = put a file here)
/apps/<slug>.ver       installed version    (written by catalog installs)
/apps/data/<slug>/     app data             (survives updates + reflashes)
/apps/catalog.url      optional catalog URL override (one line)
/apps/catalog.json     cached copy of the last fetched catalog (also
                       shipped in the flash package); resolves display
                       names for installed apps offline
```

The App Manager scans `/apps`, and installs/updates from a JSON catalog
(default: this repo's `store/catalog.json` on raw.githubusercontent.com;
downloads are staged to `.tmp` and renamed, so a failed download never
breaks an existing install). The catalog server is editable on-device
(「目录服务器」 → URL keyboard, stored in `/apps/catalog.url`; clearing the
field restores the default). Three install routes reach the same place:
the online catalog, 「从本地安装」 (picks a `.eapp` from the card through the
File Manager), and 「从浏览器安装」, which reuses the existing web file
manager — no new transport. Opening a `.eapp` in the File Manager installs
it too. Firmware reflash does not touch `/apps`, which
is the point: apps and their data live outside the firmware image.

Bluetooth transport was considered and rejected: the BLE stack costs 50 KB+
of the 380 KB RAM ceiling (`lib_ignore = BLE`), against Wi-Fi paths that
already exist.

## Building an app

```bash
cd sdk/dynapp
RISCV_TOOLCHAIN=~/.platformio/packages/toolchain-riscv32-esp/bin \
  ./build-eapp.sh apps/sysmon          # → out/sysmon.eapp
```

`build-eapp.sh` compiles `rv32imc` `-Os -fPIC -nostdlib`, links twice
against `eapp.lds.in` (pass 2 pins the data base to `align16(text)`), and
runs `verify-eapp.py`, which re-implements every loader acceptance check
host-side — a bad image fails the build, not the device. Samples: `sysmon`
(live battery/heap/uptime dashboard) and `life` (Conway, touch-editable,
state persisted through the sandbox API).

## Playing media without audio hardware

The Xteink boards declare `NO_AUDIO` (`FREEINK_CAP_AUDIO` covers only Murphy
and M5), and the C3's SoC caps list `SOC_BLE_SUPPORTED` with no Bluetooth
Classic — so there is no codec to play through and no A2DP for headphones.
The music app instead runs the classic three-box DLNA model, where the audio
never touches this chip:

1. `ssdp_discover` M-SEARCHes for `MediaRenderer` devices and the app scrapes
   each description for its AVTransport `controlURL`.
2. `media_publish` hands the firmware an absolute SD path. `DynAppMediaServer`
   (a ~200-line read-only listener on port 8081) starts, and returns the URL a
   renderer should fetch. Only audio/video extensions are served, only the one
   currently published path is reachable, and Range requests are answered with
   206 because renderers seek and resume routinely.
3. `http_post` carries the SOAP actions (`SetAVTransportURI`, `Play`, `Pause`,
   `Seek`, `SetVolume`, `GetPositionInfo`) to the renderer, which pulls the
   track straight off the card.

Two host-side consequences: `DynAppActivity::loop()` pumps the listener each
frame, and `preventAutoSleep()` reports true while a track is published so the
idle timer cannot cut a song off mid-stream. The listener and the published
path are dropped when the app unloads.

The SDK also ships `libapp` (menu, header, hint bar, the standard About
overlay, JSON scanners) so ports stay compact, and `libmini` adds the
64-bit-integer and `strchr`/`snprintf` helpers GCC expects. There is no
floating point: soft-float would need libgcc, which cannot be linked, so
apps use fixed-point integer math (see the calculator and exchange-rate
apps).

The `CpApi` grew a v1-appended block (probe `api->size`): centered/wrapped
text, the device RTC, and networking — `wifi_ensure()` connects headlessly to
a saved network (no Activity launch, so an app can fetch from inside its own
loop) and `http_get()` does an HTTPS GET into a caller buffer. The host powers
Wi-Fi down when a network app exits.

Constraints for app authors: C only (no C++ runtime), no libc beyond the
SDK mini-libc/libapp, no floating point, static + stack memory only (no malloc
in v1), image ≤ 96 KB, and all firmware access through the passed `CpApi`
pointer.

`on_loop` should return promptly, with one documented exception: a *blocking
socket read* is safe for as long as the server takes. Nothing is subscribed to
the task watchdog on this build (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`,
`PANIC=y`, but `CHECK_IDLE_TASK_CPU0` unset), and a blocking read yields the
CPU, so the idle task keeps running — which is why multi-MB OTA downloads
already work. A 30-second LLM call is fine; a 30-second busy-loop is a reset.

## Appended ABI: text entry, shared config, absolute reads

The fourth appended block is what an app needs to talk to a cloud service
from a device with no keyboard:

* `text_input_begin` / `text_input_result` — the host's on-screen keyboard.
  An app cannot block waiting for typing, so this is request-then-poll:
  `begin()` once, return `CP_LOOP_IDLE`, then `result()` each frame (1 ready,
  0 still open, -1 cancelled, -2 nothing requested). `DynAppActivity::loop()`
  picks the request up before calling `on_loop` and pushes
  `KeyboardEntryActivity`. **Push does not call `onExit`** (only Replace
  does, `ActivityManager.cpp:187`), so the `.eapp` stays loaded and its state
  is exactly where it was when the keyboard closes. `maxLength` counts
  *bytes*, not characters.
* `shared_get` / `shared_set` — a cross-app key/value store under
  `/apps/data/_shared/`, keys `[a-z0-9_]`. Deliberately *not* sandboxed per
  app: an API key typed once on an e-reader should work everywhere. This is
  the one place apps are allowed to share state.
* `file_read_abs` — read-only access to an absolute SD path with an `offset`,
  so an app can page through a file far larger than RAM (`..` rejected).
* `CP_HTTP_ERR_STATUS` (-5) joins the `http_*` error codes: the server
  answered but not with 2xx, and `buf` still holds the body — which is where
  an API puts the actual reason (bad key, no quota). Distinguishing it from a
  dead socket is the difference between a useful error and "failed".

`http_post` now reads the response with `esp_http_client_read_response()`.
A single `esp_http_client_read()` returns one TCP segment, which silently
truncated anything over an MTU — fine for a SOAP ack, wrong for a multi-KB
answer.

### The CpApi table must be complete

`kApi` is built with **designated initializers**, and `tableIsComplete()`
scans it for NULL slots on every `bind()`. This is not ceremony. An ABI
append that adds members to `CpApi` but forgets to add them to the table
leaves NULLs that no app can detect — `CpApi.size` is `sizeof(CpApi)`, so
size-probing reports the entries as present — and the app calls straight
into address 0. That shipped once: the table stopped at `apiHttpGet` while
the struct had grown through `media_publish`, so the music app's
`ssdp_discover` call was a NULL dereference. Naming every member makes the
omission visible in review; the scan catches it regardless, including for
entries that do not exist yet.

## AI apps

No local model is possible on 380 KB of RAM, so the AI apps call an
OpenAI-compatible `/chat/completions` endpoint — the shape DeepSeek, 智谱,
月之暗面, 通义千问, OpenAI and a LAN-local llama.cpp/Ollama server all speak.
The provider is the user's choice; nothing is hard-wired to a vendor.

The design constraint that matters is not the model, it is the keyboard.
Typing on an e-reader is painful, so a general-purpose chatbot is close to
the *worst* fit for this device. The apps that work here either need almost
no typing, or operate on content already on the card:

| App | Typing | What it does |
|---|---|---|
| AI 助手 (`aichat`) | one line, or none | Hub + config owner. One-tap prompts, several needing no typing at all; follow-ups keep context. |
| AI 词典 (`aidict`) | one word | Fixed-format word card, saved to the card. The saved list is an offline flashcard deck built from your own reading. |
| 每日一课 (`ailesson`) | none | One lesson a day. Past titles go back as context, so it is a curriculum rather than a shuffle; cached, so re-reading is free and offline. |
| 读书伴侣 (`aibook`) | none | Page a `.txt`/`.md` off the card, then ask about *this passage*: explain, summarise, discussion questions, who-is-who, hard words. |
| 灵感整理 (`ainote`) | one short line | Dated jottings; on demand the pile becomes an outline, an action list, or the theme you have been circling. |

EPUB is deliberately absent from `aibook`: it is a ZIP container, and
inflate does not belong in a bare-C `.eapp` with no libc.

### libai

`sdk/dynapp/libai` is opt-in — `build-eapp.sh` links it only for apps whose
sources `#include "ai.h"`, because it carries ~16 KB of `.bss` (request
5 KB, response 8 KB, extracted answer 3 KB) and the other apps should not
pay for it. It provides:

* **Config** in the shared store (`ai_url`, `ai_key`, `ai_model`,
  `ai_lang`), plus `ai_endpoint()` so a settings screen can show the URL
  that will really be POSTed — "why do I get 404" is almost always a
  `base_url` that lost its `/v1`.
* **`AiJob`**, a two-phase call. `ai_job_start()` queues; the app returns
  `CP_LOOP_RENDER` so the "thinking" panel is *on the panel* before
  `ai_job_pump()` blocks for 20-40 s. `ai_job_settled()` swallows the frame
  after, so a button pressed during the wait cannot act on the screen that
  replaced the busy panel.
* **JSON** build-with-escaping and a real unescaper. `app_json_str()` drops
  the backslash and keeps the letter, which turns every newline in an answer
  into a literal `n`; `libai` decodes `\n`, `\uXXXX` and surrogate pairs.
  It scopes the answer search to `"choices"` and matches `"content"` *with*
  its opening quote, so a provider that also returns `"reasoning_content"`
  cannot be mistaken for the answer.
* **`AiPager`**, laid out once into line offsets and then paged — no reflow
  per frame. Latin wraps at spaces, CJK breaks anywhere, and per-glyph
  advances are summed (exact for bitmap fonts) with an ASCII width cache.
* **`ai_scratch()`**, lending the request buffer back to the app between
  calls, so rewriting an index file costs no second multi-KB buffer.

`libapp` gained `AppList`, a scrolling list with a scrollbar, for the screens
that outgrew `app_menu_draw`'s fixed eight rows.

### Privacy and cost

Both are the user's to weigh, so both are stated plainly rather than buried:

* The API key is stored **in plain text** on the SD card, at
  `/apps/data/_shared/ai_key.txt`. Anyone who can read the card can read the
  key. Use a key you can revoke.
* Prompts leave the device. Whatever you send — a word, a book passage, your
  notes — goes to the provider you configured, under their retention policy.
  The transport is HTTPS, but **the server is not authenticated** — the
  wolfSSL transport has no CA bundle wired up, so this matches every other
  request the device makes (OPDS, WeRead, catalog and OTA downloads). It is
  encrypted against a passive listener, not against an active
  man-in-the-middle. Getting real verification would mean linking mbedTLS as
  a second TLS stack: measured at +193KB of flash, on a build already past
  90%. Use an API key you can revoke, and prefer a network you trust.
* Every call costs money at the provider. The apps are built to minimise
  that: answers are capped by `max_tokens`, `aidict` never re-queries a word
  it already has, and `ailesson` calls once per day and reads from the card
  afterwards.

## Building the whole store

```bash
./sdk/dynapp/build-store.sh              # every app → store/ + catalog.json
./sdk/dynapp/build-store.sh aichat       # just one, catalog refreshed
```

`store/catalog.json` is **generated, not hand-edited**. Each app carries its
own `app.meta` (`name` / `version` / `note`) and the byte count is read off
the built file, so a stale size can no longer ship. `CROSSMUX_STORE_BASE`
overrides the download prefix.
