/** @file
  Pointer backends: Simple Pointer (relative) and Absolute Pointer.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInputInternal.h"

/**
  Clamp Value to [0, Max].
**/
INT32
UefiGuiClamp (
  IN INT64  Value,
  IN INT32  Max
  )
{
  if (Value < 0) {
    return 0;
  }

  if (Value > Max) {
    return Max;
  }

  return (INT32)Value;
}

/**
  Convert relative counts to whole pixels, carrying the remainder.

  pixels = counts * PIXELS_PER_MM * Speed / (100 * Resolution)
**/
STATIC
INT64
UefiGuiScaleRelative (
  IN     INT32   Counts,
  IN     UINT64  Resolution,
  IN OUT INT64   *Remainder
  )
{
  INT64  Numerator;
  INT64  Denominator;
  INT64  Pixels;

  if (Resolution == 0) {
    //
    // Unknown resolution: one count is one pixel at 100 %.
    //
    Numerator   = (INT64)Counts * gUefiGuiInput.Config.PointerSpeedPercent + *Remainder;
    Denominator = 100;
  } else {
    if (Resolution > 0x100000) {
      Resolution = 0x100000;
    }

    Numerator = (INT64)Counts * UEFI_GUI_POINTER_PIXELS_PER_MM *
                gUefiGuiInput.Config.PointerSpeedPercent + *Remainder;
    Denominator = 100 * (INT64)Resolution;
  }

  //
  // C division truncates toward zero; the remainder keeps the sign of the
  // numerator, so movement in both directions is symmetric.
  //
  Pixels     = DivS64x64Remainder (Numerator, Denominator, Remainder);
  return Pixels;
}

/**
  Queue a button change.
**/
STATIC
VOID
UefiGuiQueueButton (
  IN UINT32   Button,
  IN BOOLEAN  Pressed
  )
{
  UEFI_GUI_INPUT_EVENT  Event;

  ZeroMem (&Event, sizeof (Event));
  Event.Type             = UEFI_GUI_INPUT_POINTER_BUTTON;
  Event.u.Button.Button  = Button;
  Event.u.Button.Pressed = Pressed;
  if (UefiGuiInputQueueAppend (&Event)) {
    if (Pressed) {
      gUefiGuiInput.Buttons |= Button;
    } else {
      gUefiGuiInput.Buttons &= ~Button;
    }
  }
}

/**
  Queue press / release events for every button whose state changed.
**/
STATIC
VOID
UefiGuiUpdateButtons (
  IN UINT32  NewButtons
  )
{
  UINT32  Changed;

  Changed = NewButtons ^ gUefiGuiInput.Buttons;
  if ((Changed & UEFI_GUI_POINTER_BUTTON_LEFT) != 0) {
    UefiGuiQueueButton (UEFI_GUI_POINTER_BUTTON_LEFT, (BOOLEAN)((NewButtons & UEFI_GUI_POINTER_BUTTON_LEFT) != 0));
  }

  if ((Changed & UEFI_GUI_POINTER_BUTTON_RIGHT) != 0) {
    UefiGuiQueueButton (UEFI_GUI_POINTER_BUTTON_RIGHT, (BOOLEAN)((NewButtons & UEFI_GUI_POINTER_BUTTON_RIGHT) != 0));
  }
}

/**
  Read the Simple Pointer once.
**/
STATIC
EFI_STATUS
UefiGuiSimplePointerPoll (
  VOID
  )
{
  EFI_SIMPLE_POINTER_PROTOCOL  *Pointer;
  EFI_SIMPLE_POINTER_STATE     State;
  EFI_STATUS                   Status;
  INT64                        Dx;
  INT64                        Dy;
  INT64                        Dz;
  UINT64                       Res;
  UEFI_GUI_INPUT_EVENT         Event;

  Pointer = gUefiGuiInput.SimplePointer;
  ZeroMem (&State, sizeof (State));
  Status = Pointer->GetState (Pointer, &State);
  if (Status == EFI_NOT_READY) {
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    gUefiGuiInput.Stats.DeviceErrors++;
    return EFI_DEVICE_ERROR;
  }

  gUefiGuiInput.Stats.PointerReads++;
  Dx = UefiGuiScaleRelative (State.RelativeMovementX, (Pointer->Mode != NULL) ? Pointer->Mode->ResolutionX : 0, &gUefiGuiInput.RemainderX);
  Dy = UefiGuiScaleRelative (State.RelativeMovementY, (Pointer->Mode != NULL) ? Pointer->Mode->ResolutionY : 0, &gUefiGuiInput.RemainderY);
  if ((Dx != 0) || (Dy != 0)) {
    gUefiGuiInput.CursorX = UefiGuiClamp ((INT64)gUefiGuiInput.CursorX + Dx, gUefiGuiInput.Width - 1);
    gUefiGuiInput.CursorY = UefiGuiClamp ((INT64)gUefiGuiInput.CursorY + Dy, gUefiGuiInput.Height - 1);
    UefiGuiInputQueueMove (gUefiGuiInput.CursorX, gUefiGuiInput.CursorY);
  }

  //
  // Each device keeps its own button state; the cursor's buttons are the
  // union, so one device going idle never releases the other's buttons.
  //
  gUefiGuiInput.LastRelButtons = (State.LeftButton ? UEFI_GUI_POINTER_BUTTON_LEFT : 0) |
                                 (State.RightButton ? UEFI_GUI_POINTER_BUTTON_RIGHT : 0);
  UefiGuiUpdateButtons (gUefiGuiInput.LastRelButtons | gUefiGuiInput.LastAbsButtons);

  //
  // Wheel: scaled like X/Y at 100 % (1/PIXELS_PER_MM mm of Z is one notch;
  // a USB wheel reports 1 count per notch at resolution 8, and the console
  // splitter rescales that to its own resolution). Positive Z is a turn
  // away from the user (scroll up); the queue reports positive toward the
  // user.
  //
  Dz = 0;
  if (State.RelativeMovementZ != 0) {
    Res = (Pointer->Mode != NULL) ? Pointer->Mode->ResolutionZ : 0;
    if (Res == 0) {
      Dz = State.RelativeMovementZ;
    } else {
      gUefiGuiInput.RemainderZ += (INT64)State.RelativeMovementZ * UEFI_GUI_POINTER_PIXELS_PER_MM;
      Dz = DivS64x64Remainder (gUefiGuiInput.RemainderZ, (INT64)MIN (Res, 0x100000), &gUefiGuiInput.RemainderZ);
    }
  }

  if (Dz != 0) {
    ZeroMem (&Event, sizeof (Event));
    Event.Type          = UEFI_GUI_INPUT_POINTER_WHEEL;
    Event.u.Wheel.Delta = (INT32)-Dz;
    UefiGuiInputQueueAppend (&Event);
  }

  return EFI_SUCCESS;
}

/**
  Map an absolute coordinate onto [0, Size - 1].
**/
STATIC
INT32
UefiGuiMapAbsolute (
  IN UINT64  Value,
  IN UINT64  Min,
  IN UINT64  Max,
  IN INT32   Size
  )
{
  if (Max <= Min) {
    return 0;
  }

  if (Value <= Min) {
    return 0;
  }

  if (Value >= Max) {
    return Size - 1;
  }

  return (INT32)DivU64x64Remainder (MultU64x64 (Value - Min, (UINT64)(Size - 1)) + (Max - Min) / 2, Max - Min, NULL);
}

/**
  Read the Absolute Pointer once.
**/
STATIC
EFI_STATUS
UefiGuiAbsolutePointerPoll (
  VOID
  )
{
  EFI_ABSOLUTE_POINTER_PROTOCOL  *Pointer;
  EFI_ABSOLUTE_POINTER_STATE     State;
  EFI_STATUS                     Status;

  Pointer = gUefiGuiInput.AbsolutePointer;
  if (Pointer->Mode == NULL) {
    return EFI_SUCCESS;
  }

  ZeroMem (&State, sizeof (State));
  Status = Pointer->GetState (Pointer, &State);
  if (Status == EFI_NOT_READY) {
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    gUefiGuiInput.Stats.DeviceErrors++;
    return EFI_DEVICE_ERROR;
  }

  gUefiGuiInput.Stats.PointerReads++;
  if (!gUefiGuiInput.AbsValid || (State.CurrentX != gUefiGuiInput.LastAbsX) || (State.CurrentY != gUefiGuiInput.LastAbsY)) {
    gUefiGuiInput.LastAbsX = State.CurrentX;
    gUefiGuiInput.LastAbsY = State.CurrentY;
    gUefiGuiInput.AbsValid = TRUE;
    gUefiGuiInput.CursorX  = UefiGuiMapAbsolute (State.CurrentX, Pointer->Mode->AbsoluteMinX, Pointer->Mode->AbsoluteMaxX, gUefiGuiInput.Width);
    gUefiGuiInput.CursorY  = UefiGuiMapAbsolute (State.CurrentY, Pointer->Mode->AbsoluteMinY, Pointer->Mode->AbsoluteMaxY, gUefiGuiInput.Height);
    UefiGuiInputQueueMove (gUefiGuiInput.CursorX, gUefiGuiInput.CursorY);
  }

  gUefiGuiInput.LastAbsButtons = ((State.ActiveButtons & EFI_ABSP_TouchActive) ? UEFI_GUI_POINTER_BUTTON_LEFT : 0) |
                                 ((State.ActiveButtons & EFI_ABS_AltActive) ? UEFI_GUI_POINTER_BUTTON_RIGHT : 0);
  UefiGuiUpdateButtons (gUefiGuiInput.LastRelButtons | gUefiGuiInput.LastAbsButtons);

  return EFI_SUCCESS;
}

/**
  Read every pointer device once.
**/
EFI_STATUS
UefiGuiPointerPoll (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = EFI_SUCCESS;
  if (gUefiGuiInput.SimplePointer != NULL) {
    if (EFI_ERROR (UefiGuiSimplePointerPoll ())) {
      Status = EFI_DEVICE_ERROR;
    }
  }

  if (gUefiGuiInput.AbsolutePointer != NULL) {
    if (EFI_ERROR (UefiGuiAbsolutePointerPoll ())) {
      Status = EFI_DEVICE_ERROR;
    }
  }

  return Status;
}
