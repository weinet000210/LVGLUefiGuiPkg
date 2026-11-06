/** @file
  UefiGuiInputLib - keyboard and pointer backends with a unified event queue.

  INTERFACE DRAFT (Phase 0). Keyboard is implemented in Phase 3 and pointer
  in Phase 4; signatures may still change during those reviews.

  The backends read Simple Text Input Ex (falling back to Simple Text Input),
  Simple Pointer and Absolute Pointer, and translate everything into
  UEFI_GUI_INPUT_EVENT entries in one FIFO queue. Protocol reads happen only
  when UefiGuiInputPoll() is called from the GUI context (TPL_APPLICATION);
  the library installs no notify callbacks that touch GUI state.

  This library knows nothing about LVGL. UefiGuiLib maps queue entries to
  LVGL keypad and pointer input devices.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_INPUT_LIB_H_
#define UEFI_GUI_INPUT_LIB_H_

#include <UefiGui/UefiGuiTypes.h>
#include <UefiGui/UefiGuiConfig.h>

/**
  Discover input protocols and create the event queue. Missing devices are
  not an error: the returned device mask tells which ones were found.

  @param[in]  Config         Input options.
  @param[in]  DisplayWidth   Pointer clamp range, in pixels.
  @param[in]  DisplayHeight  Pointer clamp range, in pixels.

  @retval EFI_SUCCESS           Queue created (devices may be 0).
  @retval EFI_OUT_OF_RESOURCES  Queue allocation failed.
**/
EFI_STATUS
EFIAPI
UefiGuiInputInitialize (
  IN CONST UEFI_GUI_INPUT_CONFIG  *Config,
  IN UINT32                       DisplayWidth,
  IN UINT32                       DisplayHeight
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
  Return the protocol wait events (WaitForKeyEx, WaitForInput) so the event
  loop can sleep in WaitForEvent() until input arrives.

  @param[out]     Events     Array receiving the events.
  @param[in,out]  Count      In: capacity of Events. Out: events written.
**/
EFI_STATUS
EFIAPI
UefiGuiInputGetWaitEvents (
  OUT    EFI_EVENT  *Events,
  IN OUT UINTN      *Count
  );

/**
  Drain all pending protocol input into the queue. GUI context only.
**/
EFI_STATUS
EFIAPI
UefiGuiInputPoll (
  VOID
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
  Current cursor position and pressed buttons (UEFI_GUI_POINTER_BUTTON_*).
**/
VOID
EFIAPI
UefiGuiInputGetPointerState (
  OUT INT32   *X,
  OUT INT32   *Y,
  OUT UINT32  *Buttons
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
  Release the queue and forget the protocols.
**/
VOID
EFIAPI
UefiGuiInputShutdown (
  VOID
  );

#endif // UEFI_GUI_INPUT_LIB_H_
