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
| UefiGuiRuntimeLib | `Library/UefiGuiRuntimeLib.h` | `Library/UefiGuiRuntimeLib` | memory: 0, timers: 5 |
| UefiGuiDisplayLib | `Library/UefiGuiDisplayLib.h` | `Library/UefiGuiDisplayLib` | 1 |
| LvglLib | (LVGL headers) | - | 2 |
| UefiGuiLib | `Library/UefiGuiLib.h` | - | 2, 5 |
| UefiGuiInputLib | `Library/UefiGuiInputLib.h` | - | 3, 4 |

Headers for classes without an instance are interface drafts and may change
during the phase that implements them.

## Execution model

- One execution context. Every LVGL call, every protocol read and every
  queue access happens in the application's main flow at TPL_APPLICATION.
- No threads and no busy polling. The event loop (Phase 5) sleeps in
  `WaitForEvent()` on a timer event plus the keyboard and pointer wait
  events, with a timeout taken from `lv_timer_handler()`'s next due time,
  clamped to `[MinWaitMs, MaxWaitMs]`.
- No UefiGui event notify function touches GUI state. If a notify function is
  ever needed it only sets a flag.

## Display path

Default (works with every GOP pixel format, including `PixelBltOnly`):

```text
LVGL partial draw buffer -> pixel conversion -> EFI_GRAPHICS_OUTPUT_BLT_PIXEL rows -> GOP Blt(BufferToVideo)
```

The conversion is explicit even when the byte layouts happen to match, so no
code depends on LVGL's internal color layout: every source pixel is decoded to
0x00RRGGBB (XRGB8888 by memory byte order, RGB565 with bit replication) and
stored field by field into `EFI_GRAPHICS_OUTPUT_BLT_PIXEL`. Rectangles larger
than the conversion buffer (`DrawBufferLines` display lines) are sent in
bands, one `Blt()` per band. A failed `Blt()` returns its status immediately.

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
(for example 0x80 becomes 514 in a 10-bit channel).

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

## Decisions

| Decision | Reason |
|---|---|
| LVGL v9.6.0 as a git submodule, `lv_conf.h` outside the tree | Upstream sources and license untouched; upgrades change only the submodule pointer. |
| Blt backend is the default | Frame buffer access is not guaranteed (PixelBltOnly, memory attributes). |
| Input read only in the GUI context | Keeps TPL rules simple; no locking. |
| edk2-stable202608, `GCC` and `VS2022` tags | Latest stable at project start; `GCC5` no longer exists there. |
