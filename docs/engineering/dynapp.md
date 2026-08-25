# DynApp — installable apps on the ESP32-C3

CrossMux can install, update, run and uninstall apps at runtime, without
reflashing the firmware and without touching user data. An installable app is
a single `.eapp` file on SD; the App Manager (应用管理) installs them from
the online catalog, and any file that lands in `/apps/` (web-file-manager
upload, WebDAV, manual copy) is equally an install.

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
```

The App Manager scans `/apps`, and installs/updates from a JSON catalog
(default: this repo's `store/catalog.json` on raw.githubusercontent.com;
downloads are staged to `.tmp` and renamed, so a failed download never
breaks an existing install). "从浏览器安装" reuses the existing web file
manager — no new transport. Firmware reflash does not touch `/apps`, which
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

Constraints for app authors: C only (no C++ runtime), no libc beyond the
SDK mini-libc, static + stack memory only (no malloc in v1), image ≤ 96 KB,
`on_loop` must return promptly (the watchdog is the firmware's), and all
firmware access through the passed `CpApi` pointer.
