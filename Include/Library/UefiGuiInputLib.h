/** @file
  UefiGuiInputLib - keyboard and pointer backends with a unified event queue.

  The backends read Simple Text Input Ex (falling back to Simple Text Input),
  Simple Pointer and Absolute Pointer, and translate everything into
  UEFI_GUI_INPUT_EVENT entries in one FIFO queue. Protocol reads happen only
  when UefiGuiInputPoll() is called from the GUI context (TPL_APPLICATION);
  the library installs no notify callbacks.

  Keyboard:
    - Key presses only exist in UEFI; each press is queued as a press event
      followed by a synthesized release event, both or neither.
    - Firmware key repeat arrives as further presses and is queued as such.
    - Ctrl+letter is normalized to the lower case letter with
      UEFI_GUI_KEY_MOD_CTRL, whether the firmware reports it as a letter plus
      shift state (Text Input Ex) or as a control character 0x01..0x1A
      (Simple Text Input; there 0x08, 0x09 and 0x0D stay Backspace, Tab and
      Enter).
    - Without Text Input Ex, modifiers other than Ctrl-letters are unknown;
      UefiGuiInputGetDevices() then reports UEFI_GUI_INPUT_DEVICE_KEYBOARD
      instead of UEFI_GUI_INPUT_DEVICE_KEYBOARD_EX.

  Pointer:
    - Relative movement (Simple Pointer) is scaled to pixels as
        pixels = counts * UEFI_GUI_POINTER_PIXELS_PER_MM * Speed% / 100
                 / Mode->Resolution
      (Resolution is counts per millimeter; 0 means "unknown" and one count
      is one pixel at 100%). Fractions are carried over, never lost.
    - Absolute Pointer coordinates are mapped linearly from the protocol's
      [AbsoluteMin, AbsoluteMax] range onto the display.
    - The cursor is clamped to [0, Width - 1] x [0, Height - 1].
    - Wheel (RelativeMovementZ) is scaled the same way at 100 % (one count
      at resolution 8 = one notch) and reported in notches, positive =
      toward the user (scroll down).
    - Consecutive moves are merged while they wait in the queue.

  Queue overflow drops the newest event and counts it; queued events are
  never reordered within their kind.

  This library knows nothing about LVGL. UefiGuiLib maps queue entries to
  LVGL keypad and pointer input devices.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_INPUT_LIB_H_
#define UEFI_GUI_INPUT_LIB_H_

#include <UefiGui/UefiGuiTypes.h>
#include <UefiGui/UefiGuiConfig.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/AbsolutePointer.h>

///
/// Reference pixel density for relative pointer scaling, see above.
///
#define UEFI_GUI_POINTER_PIXELS_PER_MM  8

///
/// Maximum number of wait events UefiGuiInputGetWaitEvents() returns.
///
#define UEFI_GUI_INPUT_MAX_WAIT_EVENTS  3

/**
  Discover input protocols and create the event queue. Missing devices are
  not an error: UefiGuiInputGetDevices() tells which ones were found.

  Discovery: the keyboard is the console input (gST->ConsoleInHandle, which
  the console splitter makes the union of all keyboards). For each pointer
  protocol the console splitter's instance on gST->ConsoleInHandle is used
  when at least one physical device exists; otherwise the first physical
  device; with no physical device the pointer is not used.

  @param[in]  Config         Input options (EnableKeyboard, EnablePointer,
                             EnableAbsolutePointer, PointerSpeedPercent,
                             EventQueueDepth).
  @param[in]  DisplayWidth   Pointer clamp range, in pixels (> 0).
  @param[in]  DisplayHeight  Pointer clamp range, in pixels (> 0).

  @retval EFI_SUCCESS            Queue created (devices may be 0).
  @retval EFI_ALREADY_STARTED    Already initialized.
  @retval EFI_INVALID_PARAMETER  Config is NULL, out of range, or a size is 0.
  @retval EFI_OUT_OF_RESOURCES   Queue allocation failed.
**/
EFI_STATUS
EFIAPI
UefiGuiInputInitialize (
  IN CONST UEFI_GUI_INPUT_CONFIG  *Config,
  IN UINT32                       DisplayWidth,
  IN UINT32                       DisplayHeight
  );

/**
  Same as UefiGuiInputInitialize() but with explicit protocol instances
  instead of discovery (tests, or applications that pick devices
  themselves). Any of them may be NULL; TextInEx takes precedence over
  TextIn. The Enable* switches in Config still apply.
**/
EFI_STATUS
EFIAPI
UefiGuiInputInitializeWithProtocols (
  IN CONST UEFI_GUI_INPUT_CONFIG              *Config,
  IN UINT32                                   DisplayWidth,
  IN UINT32                                   DisplayHeight,
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL        *TextInEx         OPTIONAL,
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL           *TextIn           OPTIONAL,
  IN EFI_SIMPLE_POINTER_PROTOCOL              *SimplePointer    OPTIONAL,
  IN EFI_ABSOLUTE_POINTER_PROTOCOL            *AbsolutePointer  OPTIONAL
  );

/**
  @return  UEFI_GUI_INPUT_DEVICE_* bits for the devices in use.
**/
UINT32
EFIAPI
UefiGuiInputGetDevices (
  VOID
  );

/**
  Return the protocol wait events (WaitForKeyEx or WaitForKey, and each
  pointer's WaitForInput) so the event loop can sleep in WaitForEvent()
  until input arrives.

  @param[out]     Events     Array receiving the events.
  @param[in,out]  Count      In: capacity of Events. Out: events written.

  @retval EFI_SUCCESS            Events written.
  @retval EFI_INVALID_PARAMETER  A pointer is NULL.
  @retval EFI_BUFFER_TOO_SMALL   *Count is too small; *Count is the size needed.
  @retval EFI_NOT_STARTED        Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiInputGetWaitEvents (
  OUT    EFI_EVENT  *Events,
  IN OUT UINTN      *Count
  );

/**
  Drain all pending protocol input into the queue. GUI context only.

  @retval EFI_SUCCESS       Done (the queue may be unchanged).
  @retval EFI_NOT_STARTED   Not initialized.
  @retval EFI_DEVICE_ERROR  A device reported an error; other devices were
                            still read and the error is counted.
**/
EFI_STATUS
EFIAPI
UefiGuiInputPoll (
  VOID
  );

/**
  Add an event to the queue, as if a device had reported it. A pointer move
  is clamped to the display and becomes the current cursor position; a
  button event updates the button state.

  @retval TRUE   Queued (or merged into the queued move).
  @retval FALSE  Not initialized, invalid type, or the queue is full
                 (counted as dropped).
**/
BOOLEAN
EFIAPI
UefiGuiInputEnqueue (
  IN CONST UEFI_GUI_INPUT_EVENT  *Event
  );

/**
  Remove the oldest event from the queue.

  @retval TRUE   Event returned.
  @retval FALSE  Queue empty.
**/
BOOLEAN
EFIAPI
UefiGuiInputDequeue (
  OUT UEFI_GUI_INPUT_EVENT  *Event
  );

/**
  Remove the oldest event of the given kinds (UEFI_GUI_INPUT_KIND_*),
  leaving events of other kinds in place and in order.
**/
BOOLEAN
EFIAPI
UefiGuiInputDequeueKind (
  IN  UINT32                Kinds,
  OUT UEFI_GUI_INPUT_EVENT  *Event
  );

/**
  @return  Number of queued events of the given kinds.
**/
UINT32
EFIAPI
UefiGuiInputPendingKind (
  IN UINT32  Kinds
  );

/**
  Current cursor position and pressed buttons (UEFI_GUI_POINTER_BUTTON_*)
  as of the last poll. Any output pointer may be NULL.
**/
VOID
EFIAPI
UefiGuiInputGetPointerState (
  OUT INT32   *X        OPTIONAL,
  OUT INT32   *Y        OPTIONAL,
  OUT UINT32  *Buttons  OPTIONAL
  );

/**
  Move the cursor without queueing an event (for example to center it).
  The position is clamped to the display.
**/
VOID
EFIAPI
UefiGuiInputSetPointerPosition (
  IN INT32  X,
  IN INT32  Y
  );

/**
  @return  Number of events dropped because the queue was full.
**/
UINT64
EFIAPI
UefiGuiInputGetDroppedCount (
  VOID
  );

/**
  Read the input statistics.

  @retval EFI_SUCCESS            Stats filled.
  @retval EFI_INVALID_PARAMETER  Stats is NULL.
  @retval EFI_NOT_STARTED        Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiInputGetStats (
  OUT UEFI_GUI_INPUT_STATS  *Stats
  );

/**
  Release the queue and forget the protocols. Safe to call more than once.
**/
VOID
EFIAPI
UefiGuiInputShutdown (
  VOID
  );

#endif // UEFI_GUI_INPUT_LIB_H_
