/** @file
  Common data types shared by the UefiGui library classes.

  These types are part of the public contract between UefiGui and the
  applications that use it. They deliberately contain no LVGL types so that
  application code and the UEFI backends never depend on LVGL headers.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_TYPES_H_
#define UEFI_GUI_TYPES_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

///
/// Package version, encoded as (Major << 16) | Minor.
///
#define UEFI_GUI_VERSION_MAJOR  0
#define UEFI_GUI_VERSION_MINOR  1
#define UEFI_GUI_VERSION        ((UEFI_GUI_VERSION_MAJOR << 16) | UEFI_GUI_VERSION_MINOR)

///
/// Display backend selection.
///
typedef enum {
  ///
  /// Pick the safest backend for the current GOP mode. Today this always
  /// resolves to UefiGuiDisplayBackendBlt.
  ///
  UefiGuiDisplayBackendAuto = 0,
  ///
  /// Render into a private buffer and copy rectangles with GOP Blt().
  /// Works with every GOP pixel format, including PixelBltOnly.
  ///
  UefiGuiDisplayBackendBlt,
  ///
  /// Optional direct frame buffer backend. Only selected when the frame
  /// buffer has been validated; never assumed to be available.
  ///
  UefiGuiDisplayBackendFrameBuffer,
  UefiGuiDisplayBackendMax
} UEFI_GUI_DISPLAY_BACKEND;

///
/// Snapshot of the active GOP mode, as reported by UefiGuiGetDisplayInfo().
///
typedef struct {
  UINT32                       ModeNumber;
  UINT32                       MaxMode;
  UINT32                       HorizontalResolution;
  UINT32                       VerticalResolution;
  UINT32                       PixelsPerScanLine;
  EFI_GRAPHICS_PIXEL_FORMAT    PixelFormat;
  EFI_PIXEL_BITMASK            PixelInformation;
  EFI_PHYSICAL_ADDRESS         FrameBufferBase;
  UINTN                        FrameBufferSize;
  UEFI_GUI_DISPLAY_BACKEND     ActiveBackend;
} UEFI_GUI_DISPLAY_INFO;

///
/// Input devices discovered by the input backend. Bit flags.
///
#define UEFI_GUI_INPUT_DEVICE_KEYBOARD_EX       BIT0  ///< Simple Text Input Ex
#define UEFI_GUI_INPUT_DEVICE_KEYBOARD          BIT1  ///< Simple Text Input (fallback)
#define UEFI_GUI_INPUT_DEVICE_SIMPLE_POINTER    BIT2  ///< Simple Pointer (relative)
#define UEFI_GUI_INPUT_DEVICE_ABSOLUTE_POINTER  BIT3  ///< Absolute Pointer

///
/// Kind of event stored in the unified input queue.
///
typedef enum {
  UEFI_GUI_INPUT_KEY,
  UEFI_GUI_INPUT_POINTER_MOVE,
  UEFI_GUI_INPUT_POINTER_BUTTON,
  UEFI_GUI_INPUT_POINTER_WHEEL
} UEFI_GUI_INPUT_TYPE;

///
/// Modifier flags carried by key events (independent of UEFI shift state bits).
///
#define UEFI_GUI_KEY_MOD_SHIFT  BIT0
#define UEFI_GUI_KEY_MOD_CTRL   BIT1
#define UEFI_GUI_KEY_MOD_ALT    BIT2
#define UEFI_GUI_KEY_MOD_LOGO   BIT3

///
/// Pointer buttons.
///
#define UEFI_GUI_POINTER_BUTTON_LEFT   BIT0
#define UEFI_GUI_POINTER_BUTTON_RIGHT  BIT1

typedef struct {
  ///
  /// UEFI scan code (SCAN_*), or SCAN_NULL for a printable character.
  ///
  UINT16     ScanCode;
  ///
  /// UCS-2 character, or 0 when the key has no character.
  ///
  CHAR16     UnicodeChar;
  ///
  /// UEFI_GUI_KEY_MOD_* flags valid when the key was read.
  ///
  UINT32     Modifiers;
  ///
  /// TRUE for press, FALSE for release. UEFI keyboards usually report
  /// presses only; the input backend synthesizes the release.
  ///
  BOOLEAN    Pressed;
} UEFI_GUI_KEY_EVENT;

typedef struct {
  ///
  /// Absolute cursor position in display pixels after this movement,
  /// already clamped to the display.
  ///
  INT32    X;
  INT32    Y;
} UEFI_GUI_POINTER_MOVE_EVENT;

typedef struct {
  UINT32     Button;    ///< One UEFI_GUI_POINTER_BUTTON_* value.
  BOOLEAN    Pressed;
} UEFI_GUI_POINTER_BUTTON_EVENT;

typedef struct {
  INT32    Delta;       ///< Positive scrolls toward the user.
} UEFI_GUI_POINTER_WHEEL_EVENT;

///
/// One entry of the unified input event queue.
///
typedef struct {
  UEFI_GUI_INPUT_TYPE    Type;
  ///
  /// Milliseconds from the UefiGui time base when the event was queued.
  ///
  UINT64                 TimestampMs;
  union {
    UEFI_GUI_KEY_EVENT               Key;
    UEFI_GUI_POINTER_MOVE_EVENT      Move;
    UEFI_GUI_POINTER_BUTTON_EVENT    Button;
    UEFI_GUI_POINTER_WHEEL_EVENT     Wheel;
  } u;
} UEFI_GUI_INPUT_EVENT;

///
/// Event classes for UefiGuiInputDequeueKind(). Bit flags.
///
#define UEFI_GUI_INPUT_KIND_KEY      BIT0   ///< UEFI_GUI_INPUT_KEY
#define UEFI_GUI_INPUT_KIND_POINTER  BIT1   ///< move, button and wheel events
#define UEFI_GUI_INPUT_KIND_ALL      (UEFI_GUI_INPUT_KIND_KEY | UEFI_GUI_INPUT_KIND_POINTER)

///
/// Input statistics, reported by UefiGuiInputGetStats() / UefiGuiGetInputStats().
///
typedef struct {
  UINT64    KeyPresses;        ///< Key presses read from the firmware.
  UINT64    PointerReads;      ///< Pointer GetState() calls that returned data.
  UINT64    QueuedEvents;      ///< Events added to the queue.
  UINT64    CoalescedMoves;    ///< Moves merged into a queued move.
  UINT64    DroppedEvents;     ///< Events lost because the queue was full.
  UINT64    DeviceErrors;      ///< Protocol calls that returned an error.
  UINT32    QueueDepth;        ///< Capacity of the queue.
  UINT32    QueuePeak;         ///< Highest number of queued events seen.
} UEFI_GUI_INPUT_STATS;

///
/// Rendering statistics, reported by UefiGuiGetRenderStats().
///
typedef struct {
  UINT64    FrameCount;        ///< Completed LVGL refreshes (last flush of a frame).
  UINT64    FlushCount;        ///< Flush callbacks (one per rendered area or band).
  UINT64    FlushedPixels;     ///< Pixels passed to the display backend.
  UINT64    FlushErrors;       ///< Flushes whose backend call failed.
  UINT64    FlushTimeUs;       ///< Time spent converting and copying pixels.
  UINT64    LoopIterations;    ///< UefiGuiProcessEvents() iterations.
  UINT64    WaitTimeUs;        ///< Time the loop spent waiting for events.
} UEFI_GUI_RENDER_STATS;

///
/// Event loop statistics, reported by UefiGuiGetLoopStats().
///
/// Every UefiGuiProcessEvents() call is one iteration. An iteration either
/// sleeps in WaitForEvent() (and then counts exactly one wake reason) or
/// skips the sleep because work is already pending (SkippedWaits).
///
typedef struct {
  UINT64    Iterations;          ///< UefiGuiProcessEvents() calls that ran.
  UINT64    SkippedWaits;        ///< Iterations that did not sleep (work pending).
  UINT64    TimerWakes;          ///< Woken by the LVGL timer deadline (or MaxWaitMs).
  UINT64    InputWakes;          ///< Woken by a keyboard or pointer wait event.
  UINT64    ExitWakes;           ///< Woken by UefiGuiRequestExit().
  UINT64    CallbackWakes;       ///< Woken by UefiGuiPostCallback().
  UINT64    AppEventWakes;       ///< Woken by an event added with UefiGuiRegisterEvent().
  UINT64    CallbacksRun;        ///< Posted callbacks executed.
  UINT64    CallbacksRejected;   ///< UefiGuiPostCallback() calls refused (queue full).
  UINT64    CallbacksDropped;    ///< Posted callbacks discarded at shutdown.
  UINT64    AppEventsHandled;    ///< Registered event handlers executed.
  UINT64    WaitTimeUs;          ///< Time spent inside WaitForEvent().
  UINT64    BusyTimeUs;          ///< Time spent outside WaitForEvent() in the loop.
  UINT64    LongestIterationUs;  ///< Longest busy part of one iteration.
  UINT32    OpenEvents;          ///< UEFI events UefiGuiLib currently owns.
  UINT32    RegisteredEvents;    ///< Application events currently registered.
} UEFI_GUI_LOOP_STATS;

///
/// Memory backend statistics, reported by UefiGuiGetMemoryStats().
///
typedef struct {
  UINT64    CurrentBytes;      ///< Bytes currently allocated (payload only).
  UINT64    PeakBytes;         ///< Highest value CurrentBytes reached.
  UINT64    LimitBytes;        ///< Active limit, 0 when unlimited.
  UINT64    AllocationCount;   ///< Successful allocations.
  UINT64    FreeCount;         ///< Successful frees.
  UINT64    FailureCount;      ///< Allocations refused or failed.
  UINT64    InvalidFreeCount;  ///< Frees of pointers not owned by UefiGui.
} UEFI_GUI_MEMORY_STATS;

#endif // UEFI_GUI_TYPES_H_
