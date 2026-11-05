/** @file
  InputTest - Phase 3 (keyboard) and Phase 4 (pointer) tests.

  1. Unit tests (UnitTests.c): UefiGuiInputLib with scripted fake devices:
     modifiers, Ctrl normalization, Simple Text Input fallback, press /
     release pairs, queue overflow, ordering, relative scaling, remainders,
     clamping, buttons, wheel, absolute mapping; the LVGL key mapping.
  2. GUI tests (GuiTests.c): the real firmware devices through LVGL. The
     test asks the host to press keys and move the mouse by writing
     "UEFIGUI-CMD <seq> <commands>" lines to the serial port
     (scripts/qemu-driver.py executes them through the QEMU monitor), then
     checks what LVGL did: focus, clicks, text, Escape, shortcuts, page
     scrolling, cursor position and clamping, left and right clicks, wheel.

  The last console line is "UEFIGUI-INPUTTEST: PASS" or "... FAIL".

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "InputTest.h"

#define MAX_RESULTS  200

typedef struct {
  CONST CHAR16    *Name;
  BOOLEAN         Passed;
  BOOLEAN         HasValue;
  INT64           Value;
} RESULT;

STATIC RESULT  mResults[MAX_RESULTS];
STATIC UINTN   mResultCount;

VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed
  )
{
  if (mResultCount < MAX_RESULTS) {
    mResults[mResultCount].Name     = Name;
    mResults[mResultCount].Passed   = Passed;
    mResults[mResultCount].HasValue = FALSE;
    mResultCount++;
  }
}

VOID
CheckValue (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed,
  IN INT64         Value
  )
{
  Check (Name, Passed);
  if (mResultCount > 0) {
    mResults[mResultCount - 1].HasValue = TRUE;
    mResults[mResultCount - 1].Value    = Value;
  }
}

EFI_STATUS
EFIAPI
InputTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN  Index;
  UINTN  Failures;

  Print (L"InputTest - LVGLUefiGuiPkg Phases 3 and 4\n");
  RunUnitTests ();
  RunGuiTests ();

  Failures = 0;
  for (Index = 0; Index < mResultCount; Index++) {
    if (!mResults[Index].Passed) {
      Failures++;
    }

    if (mResults[Index].HasValue) {
      Print (L"  [%s] %s: %ld\n", mResults[Index].Passed ? L"PASS" : L"FAIL", mResults[Index].Name, mResults[Index].Value);
    } else {
      Print (L"  [%s] %s\n", mResults[Index].Passed ? L"PASS" : L"FAIL", mResults[Index].Name);
    }
  }

  Print (L"%lu checks, %lu failed\n", (UINT64)mResultCount, (UINT64)Failures);
  Print (L"UEFIGUI-INPUTTEST: %s\n", (Failures == 0) ? L"PASS" : L"FAIL");
  return (Failures == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
