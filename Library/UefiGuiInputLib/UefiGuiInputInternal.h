/** @file
  Internal state of UefiGuiInputLib.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_INPUT_INTERNAL_H_
#define UEFI_GUI_INPUT_INTERNAL_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiGuiInputLib.h>
#include <Library/UefiGuiRuntimeLib.h>

///
/// Upper bound of key strokes read per poll, so a stuck device cannot keep
/// the GUI context busy.
///
#define UEFI_GUI_MAX_KEYS_PER_POLL  128

typedef struct {
  BOOLEAN                              Initialized;
  UEFI_GUI_INPUT_CONFIG                Config;
  INT32                                Width;
  INT32                                Height;

  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL    *TextInEx;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL       *TextIn;
  EFI_SIMPLE_POINTER_PROTOCOL          *SimplePointer;
  EFI_ABSOLUTE_POINTER_PROTOCOL        *AbsolutePointer;
  UINT32                               Devices;

  //
  // Queue: ring buffer of Capacity entries.
  //
  UEFI_GUI_INPUT_EVENT                 *Queue;
  UINT32                               Capacity;
  UINT32                               Head;     ///< Oldest entry.
  UINT32                               Count;

  //
  // Pointer state.
  //
  INT32                                CursorX;
  INT32                                CursorY;
  UINT32                               Buttons;
  INT64                                RemainderX;   ///< Scaled-count remainders.
  INT64                                RemainderY;
  INT64                                RemainderZ;
  UINT64                               LastAbsX;
  UINT64                               LastAbsY;
  UINT32                               LastAbsButtons;
  UINT32                               LastRelButtons;
  BOOLEAN                              AbsValid;

  UEFI_GUI_INPUT_STATS                 Stats;
} UEFI_GUI_INPUT_CONTEXT;

extern UEFI_GUI_INPUT_CONTEXT  gUefiGuiInput;

//
// UefiGuiInputQueue.c
//

/**
  @return  Free queue slots.
**/
UINT32
UefiGuiInputFreeSlots (
  VOID
  );

/**
  Append without the pointer-state side effects of UefiGuiInputEnqueue().
**/
BOOLEAN
UefiGuiInputQueueAppend (
  IN CONST UEFI_GUI_INPUT_EVENT  *Event
  );

/**
  Queue a move to (X, Y), merging with a queued move at the tail.
**/
BOOLEAN
UefiGuiInputQueueMove (
  IN INT32  X,
  IN INT32  Y
  );

//
// UefiGuiKeyboard.c
//

EFI_STATUS
UefiGuiKeyboardPoll (
  VOID
  );

//
// UefiGuiPointer.c
//

EFI_STATUS
UefiGuiPointerPoll (
  VOID
  );

INT32
UefiGuiClamp (
  IN INT64  Value,
  IN INT32  Max
  );

#endif // UEFI_GUI_INPUT_INTERNAL_H_
