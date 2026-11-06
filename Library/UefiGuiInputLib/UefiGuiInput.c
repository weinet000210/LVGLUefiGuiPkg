/** @file
  UefiGuiInputLib: initialization, device discovery, wait events, state and
  shutdown.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInputInternal.h"

UEFI_GUI_INPUT_CONTEXT  gUefiGuiInput;

/**
  Pick the instance of a pointer protocol to read.

  The console splitter installs the protocol on gST->ConsoleInHandle as the
  union of all devices, even when there is none; so the splitter instance is
  used only when a physical device exists, and a physical device is used
  directly when the splitter is not there.
**/
STATIC
VOID *
UefiGuiFindPointer (
  IN EFI_GUID  *Protocol
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  VOID        *Physical;
  VOID        *Splitter;

  Handles     = NULL;
  HandleCount = 0;
  Physical    = NULL;
  Splitter    = NULL;
  Status      = gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    if (Handles[Index] == gST->ConsoleInHandle) {
      gBS->HandleProtocol (Handles[Index], Protocol, &Splitter);
    } else if (Physical == NULL) {
      gBS->HandleProtocol (Handles[Index], Protocol, &Physical);
    }
  }

  gBS->FreePool (Handles);
  if (Physical == NULL) {
    return NULL;
  }

  return (Splitter != NULL) ? Splitter : Physical;
}

/**
  Validate Config and the display size.
**/
STATIC
EFI_STATUS
UefiGuiInputValidate (
  IN CONST UEFI_GUI_INPUT_CONFIG  *Config,
  IN UINT32                       DisplayWidth,
  IN UINT32                       DisplayHeight
  )
{
  if ((Config == NULL) || (DisplayWidth == 0) || (DisplayHeight == 0) ||
      (DisplayWidth > 0x7FFF) || (DisplayHeight > 0x7FFF) ||
      (Config->PointerSpeedPercent < 10) || (Config->PointerSpeedPercent > 1000) ||
      (Config->EventQueueDepth < 8) || (Config->EventQueueDepth > 1024))
  {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/**
  Initialize with explicit protocol instances.
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
  )
{
  EFI_STATUS  Status;

  if (gUefiGuiInput.Initialized) {
    return EFI_ALREADY_STARTED;
  }

  Status = UefiGuiInputValidate (Config, DisplayWidth, DisplayHeight);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (&gUefiGuiInput, sizeof (gUefiGuiInput));
  CopyMem (&gUefiGuiInput.Config, Config, sizeof (gUefiGuiInput.Config));
  gUefiGuiInput.Width    = (INT32)DisplayWidth;
  gUefiGuiInput.Height   = (INT32)DisplayHeight;
  gUefiGuiInput.Capacity = Config->EventQueueDepth;
  gUefiGuiInput.Queue    = UefiGuiAllocateArray (gUefiGuiInput.Capacity, sizeof (UEFI_GUI_INPUT_EVENT));
  if (gUefiGuiInput.Queue == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  gUefiGuiInput.Stats.QueueDepth = gUefiGuiInput.Capacity;

  if (Config->EnableKeyboard) {
    if (TextInEx != NULL) {
      gUefiGuiInput.TextInEx = TextInEx;
      gUefiGuiInput.Devices |= UEFI_GUI_INPUT_DEVICE_KEYBOARD_EX;
    } else if (TextIn != NULL) {
      gUefiGuiInput.TextIn   = TextIn;
      gUefiGuiInput.Devices |= UEFI_GUI_INPUT_DEVICE_KEYBOARD;
    }
  }

  if (Config->EnablePointer && (SimplePointer != NULL)) {
    gUefiGuiInput.SimplePointer = SimplePointer;
    gUefiGuiInput.Devices      |= UEFI_GUI_INPUT_DEVICE_SIMPLE_POINTER;
  }

  if (Config->EnableAbsolutePointer && (AbsolutePointer != NULL)) {
    gUefiGuiInput.AbsolutePointer = AbsolutePointer;
    gUefiGuiInput.Devices        |= UEFI_GUI_INPUT_DEVICE_ABSOLUTE_POINTER;
  }

  //
  // Start in the middle of the screen.
  //
  gUefiGuiInput.CursorX     = gUefiGuiInput.Width / 2;
  gUefiGuiInput.CursorY     = gUefiGuiInput.Height / 2;
  gUefiGuiInput.Initialized = TRUE;

  //
  // Discard input that arrived before the GUI started: a pending pointer
  // delta would otherwise jump the cursor, and a stale key would act on
  // the first widget.
  //
  if (gUefiGuiInput.SimplePointer != NULL) {
    EFI_SIMPLE_POINTER_STATE  State;
    gUefiGuiInput.SimplePointer->GetState (gUefiGuiInput.SimplePointer, &State);
  }

  DEBUG ((DEBUG_INFO, "[UefiGui] input devices 0x%x\n", gUefiGuiInput.Devices));
  return EFI_SUCCESS;
}

/**
  Discover input protocols and create the event queue.
**/
EFI_STATUS
EFIAPI
UefiGuiInputInitialize (
  IN CONST UEFI_GUI_INPUT_CONFIG  *Config,
  IN UINT32                       DisplayWidth,
  IN UINT32                       DisplayHeight
  )
{
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *TextInEx;

  TextInEx = NULL;
  if ((gST->ConsoleInHandle == NULL) ||
      EFI_ERROR (gBS->HandleProtocol (gST->ConsoleInHandle, &gEfiSimpleTextInputExProtocolGuid, (VOID **)&TextInEx)))
  {
    TextInEx = NULL;
    DEBUG ((DEBUG_WARN, "[UefiGui] no Simple Text Input Ex on ConIn, using Simple Text Input (no modifier keys)\n"));
  }

  return UefiGuiInputInitializeWithProtocols (
           Config,
           DisplayWidth,
           DisplayHeight,
           TextInEx,
           gST->ConIn,
           (Config != NULL && Config->EnablePointer) ? UefiGuiFindPointer (&gEfiSimplePointerProtocolGuid) : NULL,
           (Config != NULL && Config->EnableAbsolutePointer) ? UefiGuiFindPointer (&gEfiAbsolutePointerProtocolGuid) : NULL
           );
}

/**
  Devices in use.
**/
UINT32
EFIAPI
UefiGuiInputGetDevices (
  VOID
  )
{
  return gUefiGuiInput.Devices;
}

/**
  Wait events of the devices in use.
**/
EFI_STATUS
EFIAPI
UefiGuiInputGetWaitEvents (
  OUT    EFI_EVENT  *Events,
  IN OUT UINTN      *Count
  )
{
  EFI_EVENT  List[UEFI_GUI_INPUT_MAX_WAIT_EVENTS];
  UINTN      Needed;

  if ((Events == NULL) || (Count == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGuiInput.Initialized) {
    return EFI_NOT_STARTED;
  }

  Needed = 0;
  if (gUefiGuiInput.TextInEx != NULL) {
    List[Needed++] = gUefiGuiInput.TextInEx->WaitForKeyEx;
  } else if (gUefiGuiInput.TextIn != NULL) {
    List[Needed++] = gUefiGuiInput.TextIn->WaitForKey;
  }

  if (gUefiGuiInput.SimplePointer != NULL) {
    List[Needed++] = gUefiGuiInput.SimplePointer->WaitForInput;
  }

  if (gUefiGuiInput.AbsolutePointer != NULL) {
    List[Needed++] = gUefiGuiInput.AbsolutePointer->WaitForInput;
  }

  if (*Count < Needed) {
    *Count = Needed;
    return EFI_BUFFER_TOO_SMALL;
  }

  CopyMem (Events, List, Needed * sizeof (EFI_EVENT));
  *Count = Needed;
  return EFI_SUCCESS;
}

/**
  Drain all pending protocol input into the queue.
**/
EFI_STATUS
EFIAPI
UefiGuiInputPoll (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!gUefiGuiInput.Initialized) {
    return EFI_NOT_STARTED;
  }

  Status = EFI_SUCCESS;
  if (EFI_ERROR (UefiGuiKeyboardPoll ())) {
    Status = EFI_DEVICE_ERROR;
  }

  if (EFI_ERROR (UefiGuiPointerPoll ())) {
    Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

/**
  Current cursor position and buttons.
**/
VOID
EFIAPI
UefiGuiInputGetPointerState (
  OUT INT32   *X        OPTIONAL,
  OUT INT32   *Y        OPTIONAL,
  OUT UINT32  *Buttons  OPTIONAL
  )
{
  if (X != NULL) {
    *X = gUefiGuiInput.CursorX;
  }

  if (Y != NULL) {
    *Y = gUefiGuiInput.CursorY;
  }

  if (Buttons != NULL) {
    *Buttons = gUefiGuiInput.Buttons;
  }
}

/**
  Move the cursor without queueing an event.
**/
VOID
EFIAPI
UefiGuiInputSetPointerPosition (
  IN INT32  X,
  IN INT32  Y
  )
{
  if (!gUefiGuiInput.Initialized) {
    return;
  }

  gUefiGuiInput.CursorX    = UefiGuiClamp (X, gUefiGuiInput.Width - 1);
  gUefiGuiInput.CursorY    = UefiGuiClamp (Y, gUefiGuiInput.Height - 1);
  gUefiGuiInput.RemainderX = 0;
  gUefiGuiInput.RemainderY = 0;
}

/**
  Dropped events.
**/
UINT64
EFIAPI
UefiGuiInputGetDroppedCount (
  VOID
  )
{
  return gUefiGuiInput.Stats.DroppedEvents;
}

/**
  Input statistics.
**/
EFI_STATUS
EFIAPI
UefiGuiInputGetStats (
  OUT UEFI_GUI_INPUT_STATS  *Stats
  )
{
  if (Stats == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGuiInput.Initialized) {
    return EFI_NOT_STARTED;
  }

  CopyMem (Stats, &gUefiGuiInput.Stats, sizeof (*Stats));
  return EFI_SUCCESS;
}

/**
  Release the queue and forget the protocols.
**/
VOID
EFIAPI
UefiGuiInputShutdown (
  VOID
  )
{
  if (gUefiGuiInput.Queue != NULL) {
    UefiGuiFree (gUefiGuiInput.Queue);
  }

  ZeroMem (&gUefiGuiInput, sizeof (gUefiGuiInput));
}
