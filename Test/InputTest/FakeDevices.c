/** @file
  Scripted fake keyboard and pointer protocol instances.

  Each fake returns the entries of its script, one per call, then
  EFI_NOT_READY, like a device with nothing new to report.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "InputTest.h"

STATIC
EFI_STATUS
EFIAPI
FakeReset (
  IN VOID     *This,
  IN BOOLEAN  ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FakeReadKeyEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  FAKE_TEXT_IN_EX  *Fake;

  Fake = BASE_CR (This, FAKE_TEXT_IN_EX, Protocol);
  if (Fake->Next >= Fake->Count) {
    return EFI_NOT_READY;
  }

  CopyMem (KeyData, &Fake->Script[Fake->Next++], sizeof (*KeyData));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FakeSetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
FakeRegisterKey (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN  EFI_KEY_DATA                       *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                               **NotifyHandle
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
FakeUnregisterKey (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
FakeReadKey (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  FAKE_TEXT_IN  *Fake;

  Fake = BASE_CR (This, FAKE_TEXT_IN, Protocol);
  if (Fake->Next >= Fake->Count) {
    return EFI_NOT_READY;
  }

  CopyMem (Key, &Fake->Script[Fake->Next++], sizeof (*Key));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FakeSimpleGetState (
  IN     EFI_SIMPLE_POINTER_PROTOCOL  *This,
  IN OUT EFI_SIMPLE_POINTER_STATE     *State
  )
{
  FAKE_SIMPLE_POINTER  *Fake;

  Fake = BASE_CR (This, FAKE_SIMPLE_POINTER, Protocol);
  if (Fake->Next >= Fake->Count) {
    return EFI_NOT_READY;
  }

  CopyMem (State, &Fake->Script[Fake->Next++], sizeof (*State));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FakeAbsoluteGetState (
  IN     EFI_ABSOLUTE_POINTER_PROTOCOL  *This,
  IN OUT EFI_ABSOLUTE_POINTER_STATE     *State
  )
{
  FAKE_ABSOLUTE_POINTER  *Fake;

  Fake = BASE_CR (This, FAKE_ABSOLUTE_POINTER, Protocol);
  if (Fake->Next >= Fake->Count) {
    return EFI_NOT_READY;
  }

  CopyMem (State, &Fake->Script[Fake->Next++], sizeof (*State));
  return EFI_SUCCESS;
}

/**
  Create the fakes. Their wait events are plain events that are never
  signaled; the unit tests poll directly.
**/
EFI_STATUS
FakeDevicesInitialize (
  OUT FAKE_TEXT_IN_EX        *TextInEx,
  OUT FAKE_TEXT_IN           *TextIn,
  OUT FAKE_SIMPLE_POINTER    *Simple,
  OUT FAKE_ABSOLUTE_POINTER  *Absolute
  )
{
  EFI_STATUS  Status;

  ZeroMem (TextInEx, sizeof (*TextInEx));
  ZeroMem (TextIn, sizeof (*TextIn));
  ZeroMem (Simple, sizeof (*Simple));
  ZeroMem (Absolute, sizeof (*Absolute));

  TextInEx->Protocol.Reset               = (EFI_INPUT_RESET_EX)FakeReset;
  TextInEx->Protocol.ReadKeyStrokeEx     = FakeReadKeyEx;
  TextInEx->Protocol.SetState            = FakeSetState;
  TextInEx->Protocol.RegisterKeyNotify   = FakeRegisterKey;
  TextInEx->Protocol.UnregisterKeyNotify = FakeUnregisterKey;
  TextIn->Protocol.Reset                 = (EFI_INPUT_RESET)FakeReset;
  TextIn->Protocol.ReadKeyStroke         = FakeReadKey;
  Simple->Protocol.Reset                 = (EFI_SIMPLE_POINTER_RESET)FakeReset;
  Simple->Protocol.GetState              = FakeSimpleGetState;
  Simple->Protocol.Mode                  = &Simple->Mode;
  Simple->Mode.ResolutionX               = 8;
  Simple->Mode.ResolutionY               = 8;
  Simple->Mode.ResolutionZ               = 8;
  Simple->Mode.LeftButton                = TRUE;
  Simple->Mode.RightButton               = TRUE;
  Absolute->Protocol.Reset               = (EFI_ABSOLUTE_POINTER_RESET)FakeReset;
  Absolute->Protocol.GetState            = FakeAbsoluteGetState;
  Absolute->Protocol.Mode                = &Absolute->Mode;
  Absolute->Mode.AbsoluteMaxX            = 1024;
  Absolute->Mode.AbsoluteMaxY            = 1024;
  Absolute->Mode.Attributes              = EFI_ABSP_SupportsAltActive;

  Status = gBS->CreateEvent (0, 0, NULL, NULL, &TextInEx->Protocol.WaitForKeyEx);
  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (0, 0, NULL, NULL, &TextIn->Protocol.WaitForKey);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (0, 0, NULL, NULL, &Simple->Protocol.WaitForInput);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (0, 0, NULL, NULL, &Absolute->Protocol.WaitForInput);
  }

  return Status;
}

VOID
FakeDevicesShutdown (
  IN FAKE_TEXT_IN_EX        *TextInEx,
  IN FAKE_TEXT_IN           *TextIn,
  IN FAKE_SIMPLE_POINTER    *Simple,
  IN FAKE_ABSOLUTE_POINTER  *Absolute
  )
{
  if (TextInEx->Protocol.WaitForKeyEx != NULL) {
    gBS->CloseEvent (TextInEx->Protocol.WaitForKeyEx);
  }

  if (TextIn->Protocol.WaitForKey != NULL) {
    gBS->CloseEvent (TextIn->Protocol.WaitForKey);
  }

  if (Simple->Protocol.WaitForInput != NULL) {
    gBS->CloseEvent (Simple->Protocol.WaitForInput);
  }

  if (Absolute->Protocol.WaitForInput != NULL) {
    gBS->CloseEvent (Absolute->Protocol.WaitForInput);
  }
}

VOID
FakeAddKeyEx (
  IN FAKE_TEXT_IN_EX  *Fake,
  IN UINT16           ScanCode,
  IN CHAR16           Char,
  IN UINT32           ShiftState
  )
{
  if (Fake->Count < FAKE_MAX_SCRIPT) {
    ZeroMem (&Fake->Script[Fake->Count], sizeof (EFI_KEY_DATA));
    Fake->Script[Fake->Count].Key.ScanCode              = ScanCode;
    Fake->Script[Fake->Count].Key.UnicodeChar           = Char;
    Fake->Script[Fake->Count].KeyState.KeyShiftState    = ShiftState;
    Fake->Count++;
  }
}

VOID
FakeAddKey (
  IN FAKE_TEXT_IN  *Fake,
  IN UINT16        ScanCode,
  IN CHAR16        Char
  )
{
  if (Fake->Count < FAKE_MAX_SCRIPT) {
    Fake->Script[Fake->Count].ScanCode    = ScanCode;
    Fake->Script[Fake->Count].UnicodeChar = Char;
    Fake->Count++;
  }
}

VOID
FakeAddRelative (
  IN FAKE_SIMPLE_POINTER  *Fake,
  IN INT32                Dx,
  IN INT32                Dy,
  IN INT32                Dz,
  IN BOOLEAN              Left,
  IN BOOLEAN              Right
  )
{
  if (Fake->Count < FAKE_MAX_SCRIPT) {
    Fake->Script[Fake->Count].RelativeMovementX = Dx;
    Fake->Script[Fake->Count].RelativeMovementY = Dy;
    Fake->Script[Fake->Count].RelativeMovementZ = Dz;
    Fake->Script[Fake->Count].LeftButton        = Left;
    Fake->Script[Fake->Count].RightButton       = Right;
    Fake->Count++;
  }
}

VOID
FakeAddAbsolute (
  IN FAKE_ABSOLUTE_POINTER  *Fake,
  IN UINT64                 X,
  IN UINT64                 Y,
  IN UINT32                 Buttons
  )
{
  if (Fake->Count < FAKE_MAX_SCRIPT) {
    ZeroMem (&Fake->Script[Fake->Count], sizeof (EFI_ABSOLUTE_POINTER_STATE));
    Fake->Script[Fake->Count].CurrentX      = X;
    Fake->Script[Fake->Count].CurrentY      = Y;
    Fake->Script[Fake->Count].ActiveButtons = Buttons;
    Fake->Count++;
  }
}
