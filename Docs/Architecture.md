# Architecture

## Layers

```text
+----------------------------------------------------------+
| Application (UefiGuiDemo, UefiNotepad, ...)              |
|   app logic, data model, LVGL widgets                    |
+---------------------------+------------------------------+
| UefiGuiLib (public API)   | LVGL v9 (LvglLib)            |
|   init / run / shutdown,  |   widgets, layout, themes,   |
|   LVGL display + indev    |   software renderer          |
|   glue, event loop        |                              |
+-------------+-------------+--------------+---------------+
| DisplayLib  | InputLib                   | RuntimeLib    |
|  GOP, Blt,  |  Text Input Ex, Pointer,   |  memory,      |
|  pixel conv |  unified event queue       |  timers/events|
+-------------+----------------------------+---------------+
| UEFI Boot Services and protocols                         |
+----------------------------------------------------------+
```

Rules:

- Only UefiGuiLib and the LVGL glue include LVGL headers. DisplayLib,
  InputLib and RuntimeLib know nothing about LVGL and can be tested alone.
- Applications include `UefiGuiLib.h` plus LVGL headers for widgets. They
  never touch GOP or input protocols.
- The backends do not depend on each other. UefiGuiLib wires them together.
- All libraries link statically through library classes. There is no DXE
  driver and no custom protocol.

## Library classes

| Class | Header | Instance | Phase |
|---|---|---|---|
| UefiGuiRuntimeLib | `Library/UefiGuiRuntimeLib.h` | `Library/UefiGuiRuntimeLib` | memory: 0, time base: 2 |
| UefiGuiDisplayLib | `Library/UefiGuiDisplayLib.h` | `Library/UefiGuiDisplayLib` | 1 |
| LvglLib | `Library/LvglLib.h` (`<lvgl.h>`) | `Library/LvglLib` | 2 |
| UefiGuiLib | `Library/UefiGuiLib.h` | `Library/UefiGuiLib` | 2, input in 3/4, loop refinements in 5 |
| UefiGuiInputLib | `Library/UefiGuiInputLib.h` | - | 3, 4 |

Headers for classes without an instance are interface drafts and may change
during the phase that implements them.

## Execution model

- One execution context. Every LVGL call, every protocol read and every
  queue access happens in the application's main flow at TPL_APPLICATION.
- Time base: the CPU time stamp counter, calibrated once against
  `gBS->Stall()` (50 ms). UEFI timer events only fire at the platform tick
  (10 ms on OVMF), so they are not used as a clock. `lv_tick` reads it
  through `lv_tick_set_cb()`.

### Event loop (Phase 5)

Each `UefiGuiProcessEvents()` call:

1. runs the callbacks queued with `UefiGuiPostCallback()` (only those queued
   before this step started, so a callback that re-posts itself runs once
   per iteration and cannot starve the loop),
2. reads the keyboard and pointer into the unified queue and hands every
   queued event to LVGL (`lv_indev_read()`),
3. runs `lv_timer_handler()`,
4. unless work is already pending, sleeps in `gBS->WaitForEvent()` on:

   | Index | Event | Signaled by |
   |---|---|---|
   | 0 | wait timer (`EVT_TIMER`, no notify) | next LVGL timer, clamped to `[MinWaitMs, MaxWaitMs]` (defaults 1 and 100 ms) |
   | 1 | exit event | `UefiGuiRequestExit()` |
   | 2 | wake event | `UefiGuiPostCallback()` |
   | 3.. | keyboard / pointer wait events | firmware input drivers |
   | .. | application events | `UefiGuiRegisterEvent()` (up to 8) |

5. runs the handlers of signaled application events. The other signaled
   events are collected with `CheckEvent()`; stale signals of the timer,
   exit and wake events are consumed so the next wait is not cut short.

`UefiGuiRun()` repeats this until an exit is requested, consumes the request
and returns `EFI_SUCCESS`, so it can be called again.

What keeps an idle screen asleep:

- The LVGL keypad and pointer input devices run in `LV_INDEV_MODE_EVENT`:
  no periodic read timer; step 2 reads only when input arrived. LVGL itself
  resumes the pointer read timer while a button is held (long press,
  scrolling) and pauses it on release.
- LVGL pauses its display refresh timer while nothing is invalidated. This
  only happens with `LV_USE_PERF_MONITOR` off, so the performance monitor is
  not compiled in by default (`UEFI_GUI_PERF_MONITOR`, see BuildGuide.md).
- LVGL pauses its animation timer when no animation runs. A focused text
  area blinks its cursor with an endless animation, so it keeps the loop at
  frame rate while focused; that is LVGL behavior.
- With nothing due, the loop sleeps `MaxWaitMs` (100 ms): 10 wake-ups a
  second. Every real wake source is an event, so `MaxWaitMs` is only a
  safety net for input drivers whose wait event does not fire.
- While `WaitForEvent()` waits, the DXE core idle loop halts the CPU until
  the next interrupt.

Timer resolution: `SetTimer()` fires on platform timer ticks (10 ms on
OVMF; RuntimeTest prints the value read from the Timer Architectural
Protocol). LVGL measures a timer's period from its previous run, so each
run of an LVGL timer is 0 to one tick late and a 20 ms LVGL timer runs at
roughly 20 / (20 + tick / 2) of its nominal rate. Animations are time
based and keep their duration (600 ms animation: 620 ms measured).

### TPL rules

- Everything in `UefiGuiLib.h`, and every LVGL API, is called at
  TPL_APPLICATION from the application's main flow.
- Exceptions: `UefiGuiRequestExit()` and `UefiGuiPostCallback()` may be
  called from UEFI event notification functions at up to TPL_NOTIFY. They
  set a flag or append to a fixed 32-entry ring (TPL raised to TPL_NOTIFY
  around the access, no allocation) and call `SignalEvent()`.
- UefiGui's own events have no notification functions. A notification
  function in the application must never touch LVGL; it posts a callback.
- `UefiGuiProcessEvents()`, `UefiGuiRun()` and `UefiGuiShutdown()` refuse
  to run from inside the loop (LVGL event handlers, the key handler, posted
  callbacks, application event handlers): the first two return
  `EFI_ACCESS_DENIED`, shutdown does nothing and records the error.

### Shutdown

`UefiGuiShutdown()` marks the framework stopped (notification functions
calling `UefiGuiPostCallback()` / `UefiGuiRequestExit()` get
`EFI_NOT_STARTED` from then on), then releases in order: LVGL input devices,
group and cursor; the input queue; all LVGL objects, the display and its
draw buffers (`lv_deinit()`); the wait timer, exit and wake events (queued
callbacks are discarded and counted); the GOP Blt buffer; and restores the
GOP mode that was active before `UefiGuiInitialize()` if another one was
selected. Registered application events are forgotten, not closed: they
belong to the application. `UefiGuiGetLoopStats()` still returns the final
statistics of the session after shutdown; RuntimeTest checks that
`OpenEvents` is 0 and that UefiGui memory is back to its starting value
after every session.

## Display path

Default (works with every GOP pixel format, including `PixelBltOnly`):

```text
LVGL partial draw buffer -> pixel conversion -> EFI_GRAPHICS_OUTPUT_BLT_PIXEL rows -> GOP Blt(BufferToVideo)
```

The conversion is explicit, so no code depends on LVGL's internal color
layout. An XRGB8888 source (LVGL's format, memory order Blue, Green, Red,
unused) is converted one 32-bit word per pixel with the unused byte cleared;
this relies on the byte layout UEFI defines for
`EFI_GRAPHICS_OUTPUT_BLT_PIXEL` (Blue, Green, Red, Reserved) on a
little-endian CPU, which `STATIC_ASSERT`s in `UefiGuiGopBlt.c` check at
compile time. RGB565 sources are decoded per pixel with bit replication.
Rectangles larger than the conversion buffer (`DrawBufferLines` display
lines) are sent in bands, one `Blt()` per band. A failed `Blt()` returns its
status immediately.

Phase 6 measured the conversion as the largest part of a flush and replaced
the original per-pixel, per-field conversion with the word loop; see
[Performance.md](Performance.md) for the numbers before and after.

GOP selection: the GOP on `gST->ConsoleOutHandle` if it has one, otherwise
the first GOP installed. Inside the UEFI Shell the console handle is the
Shell's own and carries no GOP, so the first GOP is used.

Mode selection: with `PreferredWidth/Height` set, the largest mode that fits is
chosen; the original mode is restored by `UefiGuiDisplayShutdown()`. If no
mode fits, the current mode is kept.

### Frame buffer backend (optional)

Selected only with `UefiGuiDisplayBackendFrameBuffer`; `Auto` never picks it.
Initialization returns `EFI_UNSUPPORTED` unless all checks pass:

1. Pixel format is RGB8, BGR8 or PixelBitMask with contiguous,
   non-overlapping masks (16-bit or 32-bit pixels). PixelBltOnly is refused.
2. `FrameBufferBase != 0` and `PixelsPerScanLine >= HorizontalResolution`.
3. `FrameBufferSize >= PixelsPerScanLine * BytesPerPixel * VerticalResolution`.
4. The GCD memory space map covers the whole range with present memory that
   has neither `EFI_MEMORY_RO` nor `EFI_MEMORY_RP`.
5. A write/read-back probe on the first pixel succeeds (the pixel is
   restored).

PixelBitMask channels wider or narrower than 8 bits are scaled with rounding
(for example 0x80 becomes 514 in a 10-bit channel). An XRGB8888 source on a
32-bit frame buffer whose channels are whole bytes at the BGR8 or RGB8
positions is written one word per pixel without the generic encoder.

In QEMU the two backends are close: OVMF's `Blt()` is a memory copy inside the
firmware. A full-screen XRGB8888 flush took 20.0 ms through the frame buffer
against 17.6 ms through `Blt()` in a local run, and 5.37 ms against 5.60 ms on
the CI runner (at most about 4% in favour of the frame buffer); with an RGB565
source the frame buffer is about twice as slow. See
[Performance.md](Performance.md). Whether direct access pays off on a given
machine has to be measured there with PerfTest.

## Memory

All UefiGui and LVGL memory goes through `UefiGuiAllocate()` /
`UefiGuiFree()` (UEFI pool). Each block carries a 16-byte header with a
signature and the requested size, which provides:

- `realloc` semantics (LVGL needs them; the UEFI pool has none),
- detection of frees of foreign or already freed pointers,
- statistics (current, peak, failures) and an optional limit
  (`PcdUefiGuiMemoryLimit` or `UefiGuiSetMemoryLimit()`).

LVGL is configured with `LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM` (hooks into
the functions above) and `LV_STDLIB_BUILTIN` for string and sprintf, so it does
not depend on a C library.

## LVGL port

`UefiGuiInitialize()` calls `lv_init()`, installs the tick, log and memory
hooks, and creates one LVGL display the size of the GOP mode:

- color format `LV_COLOR_FORMAT_XRGB8888` (byte order B, G, R, X), which
  UefiGuiDisplayLib converts for every GOP pixel format;
- `LV_DISPLAY_RENDER_MODE_PARTIAL` with one (or, with `DoubleBuffer`, two)
  draw buffers of `DrawBufferLines` full lines (default 64: 256 KiB at 1024
  wide);
- the flush callback validates the area, calls `UefiGuiDisplayFlushRect()`
  with LVGL's stride for that width (`lv_draw_buf_width_to_stride()`), counts
  frames, flushes, pixels, errors and time, and always ends with
  `lv_display_flush_ready()`, also when the backend failed, so LVGL never
  waits for a flush that cannot finish. Errors go to
  `UefiGuiGetLastError()` and `UEFI_GUI_RENDER_STATS.FlushErrors`.

GOP `Blt()` is synchronous, so `lv_display_flush_ready()` is called from the
callback itself.

### LVGL configuration

`Library/LvglLib/lv_conf.h` is generated from LVGL's template by
`scripts/gen-lv-conf.py`, and `LvglLib.inf` by `scripts/gen-lvgl-inf.py`.
Both scripts fail if an LVGL update renames a setting they rely on.

| Area | Setting |
|---|---|
| OS, threads | `LV_OS_NONE`; all LVGL calls in the GUI context |
| Rendering | software (`LV_USE_DRAW_SW`), no GPU units, refresh period 16 ms |
| Memory | `LV_STDLIB_CUSTOM` -> `UefiGuiAllocate()`/`UefiGuiReallocate()`/`UefiGuiFree()` |
| string, sprintf | LVGL built-in (no C library) |
| `<inttypes.h>`, `<limits.h>` | `lv_uefi_gui_inttypes.h`, `lv_uefi_gui_limits.h` (freestanding toolchains have neither) |
| `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<stdarg.h>` | compiler-provided; GCC modules that include LVGL headers build with `-ffreestanding` |
| Compiler-emitted `memcpy`/`memset`/`memmove`/`memcmp` | `MdePkg/Library/CompilerIntrinsicsLib` |
| File systems, image decoders, LVGL's own UEFI driver | off |
| Fonts | Montserrat 14 (default), 20, 28; unscii 8 and 16 (monospace); ASCII |
| Log | `LV_LOG_LEVEL_WARN` -> `DEBUG()` |
| Assert | null and malloc checks; failure prints the location and halts (`lv_uefi_gui_assert.h`) |
| Performance monitor | off unless built with `-DUEFI_GUI_PERF_MONITOR=1` (it keeps the refresh timer running); then hidden unless `Debug.ShowPerformanceMonitor` |

x86_64-linux-gnu-gcc predefines `__linux__`; LvglLib undefines it so LVGL
does not select Linux code paths. LVGL sources are compiled unmodified with
warnings non-fatal, but undeclared functions are errors and any missing C
library symbol fails the link.

## Input

```text
 firmware                      UefiGuiInputLib                 UefiGuiLib (LVGL)
 Simple Text Input Ex  --+                                  +-> keypad indev -> default group
 (Simple Text Input)     +-> poll in GUI context -> queue --+
 Simple Pointer        --+   (no notify callbacks)          +-> pointer indev -> cursor, clicks
 Absolute Pointer      --+                                       right click event, wheel scroll
```

- **Discovery.** Keyboard: Text Input Ex on `gST->ConsoleInHandle` (the
  console splitter, so every keyboard), else `gST->ConIn` without modifier
  information. Pointers: the console splitter's instance when at least one
  physical device exists, else the first device; none when there is no
  physical device (the splitter installs its pointer protocols even then).
- **Loop.** `UefiGuiProcessEvents()` polls the devices, lets LVGL read the
  new input at once (`lv_indev_read()`), runs `lv_timer_handler()`, then
  sleeps in `WaitForEvent()` on the frame timer plus `WaitForKeyEx` and each
  pointer's `WaitForInput`.
- **Queue.** One ring buffer (`Input.EventQueueDepth`, default 64). A key
  press is queued with a synthesized release, both or neither; consecutive
  moves are merged; when full, the newest event is dropped and counted
  (`UEFI_GUI_INPUT_STATS`). The keypad and pointer read callbacks take
  their own kind of event in order (`UefiGuiInputDequeueKind()`) and use
  `continue_reading`, so LVGL sees every press and release of a burst.
- **Keys.** UEFI keys are never passed to LVGL directly:
  `UefiGuiKeyToLvgl()` maps them (arrows, Enter, Esc, Tab/Shift+Tab,
  Backspace, Delete, Home, End to `LV_KEY_*`; printable characters to UTF-8).
  Ctrl/Alt combinations are normalized (Ctrl+A arrives as `'a'` +
  `UEFI_GUI_KEY_MOD_CTRL` from both protocols) and go only to the
  application key handler. Page Up/Down scroll the focused object by a page.
  Firmware key repeat arrives as further presses.
- **Pointer.** Relative counts are scaled by the device resolution
  (`pixels = counts * 8 * Speed% / 100 / ResolutionX`, remainders carried),
  absolute coordinates mapped from the device range, the cursor clamped to
  the display. The arrow cursor is an ARGB image on LVGL's system layer.
  Right click (press and release on one object) sends a registered LVGL
  event; the wheel scrolls the scrollable object under the cursor by 40 px
  per notch.
- **Debug output** goes to the COM1 UART (`BaseDebugLibSerialPort`), never to
  ConOut: the GUI owns the screen, and a ConOut write blocks while the UEFI
  Shell's output is paused by Ctrl+S.

## Decisions

| Decision | Reason |
|---|---|
| LVGL v9.6.0 as a git submodule, `lv_conf.h` outside the tree | Upstream sources and license untouched; upgrades change only the submodule pointer. |
| Blt backend is the default | Frame buffer access is not guaranteed (PixelBltOnly, memory attributes). |
| Input read only in the GUI context | Keeps TPL rules simple; no locking. |
| Key release synthesized with each press | UEFI reports presses only; LVGL needs both. |
| QEMU tests drive real devices through the monitor | Exercises firmware drivers, console splitter and LVGL together, not just fakes. |
| edk2-stable202608, `GCC` and `VS2022` tags | Latest stable at project start; `GCC5` no longer exists there. |
