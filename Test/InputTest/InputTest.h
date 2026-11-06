/** @file
  InputTest shared declarations.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef INPUT_TEST_H_
#define INPUT_TEST_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/LvglLib.h>
#include <Library/UefiGuiDisplayLib.h>
#include <Library/UefiGuiInputLib.h>
#include <Library/UefiGuiLib.h>
#include <Library/UefiGuiRuntimeLib.h>

//
// Results (InputTest.c). Recorded while the GUI owns the screen, printed
// after shutdown.
//
VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed
  );

VOID
CheckValue (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed,
  IN INT64         Value
  );

//
// Fake devices (FakeDevices.c).
//
#define FAKE_MAX_SCRIPT  32

typedef struct {
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL    Protocol;
  EFI_KEY_DATA                         Script[FAKE_MAX_SCRIPT];
  UINTN                                Count;
  UINTN                                Next;
} FAKE_TEXT_IN_EX;

typedef struct {
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL       Protocol;
  EFI_INPUT_KEY                        Script[FAKE_MAX_SCRIPT];
  UINTN                                Count;
  UINTN                                Next;
} FAKE_TEXT_IN;

typedef struct {
  EFI_SIMPLE_POINTER_PROTOCOL          Protocol;
  EFI_SIMPLE_POINTER_MODE              Mode;
  EFI_SIMPLE_POINTER_STATE             Script[FAKE_MAX_SCRIPT];
  UINTN                                Count;
  UINTN                                Next;
} FAKE_SIMPLE_POINTER;

typedef struct {
  EFI_ABSOLUTE_POINTER_PROTOCOL        Protocol;
  EFI_ABSOLUTE_POINTER_MODE            Mode;
  EFI_ABSOLUTE_POINTER_STATE           Script[FAKE_MAX_SCRIPT];
  UINTN                                Count;
  UINTN                                Next;
} FAKE_ABSOLUTE_POINTER;

EFI_STATUS
FakeDevicesInitialize (
  OUT FAKE_TEXT_IN_EX        *TextInEx,
  OUT FAKE_TEXT_IN           *TextIn,
  OUT FAKE_SIMPLE_POINTER    *Simple,
  OUT FAKE_ABSOLUTE_POINTER  *Absolute
  );

VOID
FakeDevicesShutdown (
  IN FAKE_TEXT_IN_EX        *TextInEx,
  IN FAKE_TEXT_IN           *TextIn,
  IN FAKE_SIMPLE_POINTER    *Simple,
  IN FAKE_ABSOLUTE_POINTER  *Absolute
  );

VOID
FakeAddKeyEx (
  IN FAKE_TEXT_IN_EX  *Fake,
  IN UINT16           ScanCode,
  IN CHAR16           Char,
  IN UINT32           ShiftState
  );

VOID
FakeAddKey (
  IN FAKE_TEXT_IN  *Fake,
  IN UINT16        ScanCode,
  IN CHAR16        Char
  );

VOID
FakeAddRelative (
  IN FAKE_SIMPLE_POINTER  *Fake,
  IN INT32                Dx,
  IN INT32                Dy,
  IN INT32                Dz,
  IN BOOLEAN              Left,
  IN BOOLEAN              Right
  );

VOID
FakeAddAbsolute (
  IN FAKE_ABSOLUTE_POINTER  *Fake,
  IN UINT64                 X,
  IN UINT64                 Y,
  IN UINT32                 Buttons
  );

//
// Test groups.
//
VOID
RunUnitTests (
  VOID
  );

VOID
RunGuiTests (
  VOID
  );

#endif // INPUT_TEST_H_
