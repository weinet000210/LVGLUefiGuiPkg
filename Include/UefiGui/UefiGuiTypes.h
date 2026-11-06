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
