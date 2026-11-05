# Porting Guide

How to bring LVGLUefiGuiPkg to a new machine, a new firmware, or a new
application. Build steps are in [BuildGuide.md](BuildGuide.md); the design is
in [Architecture.md](Architecture.md); measured numbers are in
[Performance.md](Performance.md).

## What the framework needs from the firmware

| Need | Protocol / service | Without it |
|---|---|---|
| Display | `EFI_GRAPHICS_OUTPUT_PROTOCOL` (any pixel format, `PixelBltOnly` included) | `UefiGuiInitialize()` returns `EFI_NOT_FOUND` |
| Keyboard | `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL`, else `EFI_SIMPLE_TEXT_INPUT_PROTOCOL` | Runs without a keypad device; `UefiGuiGetLastError()` says why |
| Mouse | `EFI_SIMPLE_POINTER_PROTOCOL` and/or `EFI_ABSOLUTE_POINTER_PROTOCOL` | Runs keyboard-only (Tab / Shift+Tab focus navigation) |
| Time base | x86 TSC, calibrated against `gBS->Stall()` | `UefiGuiInitialize()` fails (`EFI_UNSUPPORTED`) |
| Event loop | `gBS->CreateEvent`, `SetTimer`, `WaitForEvent`, `SignalEvent` | (always present in Boot Services) |
| Memory | `gBS->AllocatePool` / `FreePool` | (always present) |
| Frame buffer backend only | GCD memory space map (`DxeServicesTableLib`) | The backend refuses with `EFI_UNSUPPORTED`; the Blt backend works |

The libraries run as part of a `UEFI_APPLICATION` (a Shell application or a
boot option). They use Boot Services and must not be used after
`ExitBootServices()`.

Only X64 is built and tested. The time base reads the TSC, so another
architecture needs a new `UefiGuiTime` implementation in UefiGuiRuntimeLib
(for example the ARM generic timer); everything else is
architecture-neutral C.

## Bring-up checklist on a new machine

Copy the `.efi` files from the CI artifact (or your own build) to a FAT
USB stick and run them from the UEFI Shell, in this order. Each prints its
checks and a `PASS` / `FAIL` line; a `FAIL` names the check.

1. `UefiGuiHello.efi`: the package loads, memory and time base work.
2. `GopTest.efi`: GOP found, mode information and pixel format, Blt,
   read-back, mode switching. Its test screen shows color bars and a
   gradient: wrong colors mean a pixel-format problem, a sheared image means
   a stride (`PixelsPerScanLine`) problem.
3. `LvglPortTest.efi`: LVGL renders through the port (button and label,
   partial rendering, flush error handling).
4. `InputTest.efi`: on real hardware the host-driven part cannot run; use it
   for the unit checks and try the keyboard and mouse on its screen by hand.
   `RuntimeTest.efi` needs the host driver for its keyboard section as well.
5. `PerfTest.efi -soak 600`: the numbers for this machine and a ten-minute
   stability run. Its host-driven USB keyboard sample waits 3 s per key and
   then reports the key as missing; the other sections run unattended.

Things that differ between firmwares and what the framework does about them:

- **GOP on the console handle.** Some firmwares put the GOP on
  `gST->ConsoleOutHandle`, others (and the UEFI Shell) do not. The framework
  takes the console GOP when there is one, else the first GOP instance. For
  a specific display, call `UefiGuiDisplayInitializeWithGop()`.
- **Several GOP instances** (two monitors, a BMC display): see above; only
  one display is driven.
- **Pixel formats.** BGR8, RGB8, PixelBitMask and PixelBltOnly all work with
  the default Blt backend because the firmware converts Blt pixels.
- **Preferred resolution.** `Config.Display.PreferredWidth/Height` selects
  the largest mode that fits; the original mode is restored at shutdown.
  0 x 0 keeps the current mode.
- **Keyboards without the Ex protocol** fall back to Simple Text Input:
  Ctrl+letter still arrives (from the control characters 0x01..0x1A), but
  other modifier combinations (Alt, Ctrl with non-letters) cannot be seen.
- **Firmware key repeat.** Repeats arrive as separate presses; the
  framework does not generate its own repeat.
- **Absolute pointers** report in their own coordinate range; the framework
  scales to the display. Touch screens that report through the Absolute
  Pointer protocol therefore work as a one-button mouse.
- **No USB mouse driver** (OVMF is one example): the pointer is simply
  absent. Load `UsbMouseDxe.efi` from MdeModulePkg (built by this package's
  DSC) before the application if needed.
- **Timer tick.** `WaitForEvent()` wake-ups are only as fine as the
  firmware's timer tick (10 ms in OVMF). LVGL animations are time based and
  stay correct; periodic LVGL timers run late by up to one tick.

## Tuning

All knobs are fields of `UEFI_GUI_CONFIG` (see `UefiGui/UefiGuiConfig.h`),
filled with defaults by `UefiGuiConfigInitDefaults()`.

| Field | Default | When to change |
|---|---|---|
| `Display.Backend` | `Auto` (= Blt) | `FrameBuffer` only after PerfTest shows it is faster on that machine; it is refused when the frame buffer fails validation |
| `Display.DrawBufferLines` | 64 | More lines cut rendering overhead per frame (PerfTest: full-height buffer vs 64 lines) at `width x lines x 4` bytes each for the draw and the conversion buffers; fewer lines save memory |
| `Display.DoubleBuffer` | FALSE | GOP Blt is synchronous, so a second draw buffer gains nothing; leave it off |
| `Display.PreferredWidth/Height` | 0 x 0 | Pick a mode; full-screen redraw time grows with the pixel count (PerfTest "Resolutions") |
| `Timer.MaxWaitMs` | 100 | Upper bound of one idle sleep. Input and timers wake the loop anyway; raise it to wake less often on an idle screen |
| `Timer.MinWaitMs` | 1 | Lower bound of one sleep |
| `Input.EventQueueDepth` | 64 | Raise if `UEFI_GUI_INPUT_STATS.DroppedEvents` is ever non-zero |
| `Input.PointerSpeedPercent` | 100 | Relative mouse speed |
| `Memory.LimitBytes` | 0 (none) | Cap LVGL + framework memory; allocations beyond fail cleanly |
| `Debug.ShowPerformanceMonitor` | FALSE | Needs `UEFI_GUI_PERF_MONITOR=1` at build time; keeps the screen redrawing at frame rate |

LVGL itself is configured by the generated `Library/LvglLib/lv_conf.h`.
Change settings in `scripts/gen-lv-conf.py` (which fails loudly when an LVGL
update renames a setting) and rerun it. `LvglLib.inf` lists every LVGL source
file and lv_conf.h gates them, so enabling a widget or a font needs no INF
change; rerun `scripts/gen-lvgl-inf.py` only after an LVGL update. Every
enabled font is linked into each application, so enable only the sizes in
use.

## Updating LVGL

1. `git -C Library/LvglLib/lvgl fetch --tags && git -C Library/LvglLib/lvgl checkout vX.Y.Z`
2. `python3 scripts/gen-lv-conf.py` and `python3 scripts/gen-lvgl-inf.py`
3. Build with both toolchains; fix deprecation errors (the build uses
   `-Werror`; LVGL 9.6 deprecated `lv_list` and `lv_obj_remove_flag`, for
   example).
4. Run the tests, PerfTest included, and compare its numbers with
   [Performance.md](Performance.md).

## Porting an application onto the framework

The application owns its widgets and logic; the framework owns the display,
input, time and loop.

```c
UEFI_GUI_CONFIG  Config;

UefiGuiConfigInitDefaults (&Config);
Config.Display.PreferredWidth  = 1024;          // optional
Config.Display.PreferredHeight = 768;
Status = UefiGuiInitialize (&Config);
if (EFI_ERROR (Status)) {
  return Status;                                // no GOP, no time base, ...
}

BuildMainWindow ();                             // LVGL widget code
UefiGuiSetKeyHandler (OnShortcut, &App);        // Ctrl+S, Ctrl+O, F1, ...
Status = UefiGuiRun ();                         // until UefiGuiRequestExit()
UefiGuiShutdown ();                             // frees everything, restores GOP mode
```

Mapping from a typical text-mode or hand-drawn GOP application:

| Old code | With LVGLUefiGuiPkg |
|---|---|
| `ConOut->OutputString` / own glyph drawing | `lv_label`, `lv_textarea` (monospace: `lv_font_unscii_16`) |
| Polling `ReadKeyStroke` in a loop | Widgets receive keys through the default group; shortcuts arrive in the key handler |
| `WaitForEvent (WaitForKey)` main loop | `UefiGuiRun()`; or call `UefiGuiProcessEvents()` from your own loop |
| Own timers with `CreateEvent` + `CheckEvent` | `lv_timer_create()`, or `UefiGuiRegisterEvent()` for UEFI events (a protocol's wait event, an `EVT_TIMER`) |
| Work in a notification function that draws | Draw only in the GUI context: the notification calls `UefiGuiPostCallback()`, the callback touches LVGL |
| `Blt` of a whole screen per change | Change the widget; LVGL redraws only the invalidated area |
| File I/O | Unchanged (Simple File System); do it from callbacks, it blocks the loop while it runs |
| Exit on Esc | Key handler or `LV_KEY_ESC` handler calls `UefiGuiRequestExit()` |

Rules that keep applications correct:

- Call LVGL and UefiGui functions only from the application's main context
  at `TPL_APPLICATION`. From a notification function, use only
  `UefiGuiPostCallback()` and `UefiGuiRequestExit()`.
- Do not call `UefiGuiRun()`, `UefiGuiProcessEvents()` or
  `UefiGuiShutdown()` from inside an LVGL event handler, the key handler,
  a posted callback or an application event handler; they return
  `EFI_ACCESS_DENIED` or do nothing.
- Long operations (reading a large file) block the loop and the screen;
  split them into steps run from an LVGL timer or posted callbacks, and
  show progress.
- Unregister application events before closing them; `UefiGuiShutdown()`
  forgets them but never closes them.
- Text: LVGL renders UTF-8. The built-in fonts cover ASCII; other scripts
  need an additional font compiled into the application.

## What has been validated

QEMU 10 + OVMF only, through GitHub Actions and local runs: GOP in BGR8
with 30 modes, USB keyboard, USB boot-protocol mouse (with UsbMouseDxe or
UsbMouseAbsolutePointerDxe). Other pixel formats, PixelBltOnly and input edge
cases are covered with synthetic devices in GopTest and InputTest. Nothing
has been run on real hardware yet; the checklist above is the procedure for
doing so.
