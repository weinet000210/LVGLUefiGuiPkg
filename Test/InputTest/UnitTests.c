/** @file
  InputTest unit tests: UefiGuiInputLib with scripted fake devices, and the
  LVGL key mapping. No real device is read.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "InputTest.h"

#define W  1024
#define H  768

STATIC FAKE_TEXT_IN_EX        mKbdEx;
STATIC FAKE_TEXT_IN           mKbd;
STATIC FAKE_SIMPLE_POINTER    mMouse;
STATIC FAKE_ABSOLUTE_POINTER  mTablet;

#define SHIFT  (EFI_SHIFT_STATE_VALID | EFI_LEFT_SHIFT_PRESSED)
#define CTRL   (EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED)
#define ALT    (EFI_SHIFT_STATE_VALID | EFI_RIGHT_ALT_PRESSED)
#define NOMOD  (EFI_SHIFT_STATE_VALID)

/**
  Default input configuration with a given queue depth and speed.
**/
STATIC
VOID
MakeConfig (
  OUT UEFI_GUI_INPUT_CONFIG  *Config,
  IN  UINT32                 Depth,
  IN  UINT32                 Speed
  )
{
  UEFI_GUI_CONFIG  Full;

  UefiGuiConfigInitDefaults (&Full);
  CopyMem (Config, &Full.Input, sizeof (*Config));
  Config->EventQueueDepth     = Depth;
  Config->PointerSpeedPercent = Speed;
}

/**
  Reset all fakes' scripts.
**/
STATIC
VOID
ResetFakes (
  VOID
  )
{
  mKbdEx.Count  = mKbdEx.Next = 0;
  mKbd.Count    = mKbd.Next = 0;
  mMouse.Count  = mMouse.Next = 0;
  mTablet.Count = mTablet.Next = 0;
}

/**
  Dequeue one key event and compare it.
**/
STATIC
BOOLEAN
NextKey (
  IN UINT16   Scan,
  IN CHAR16   Char,
  IN UINT32   Mods,
  IN BOOLEAN  Pressed
  )
{
  UEFI_GUI_INPUT_EVENT  Ev;

  if (!UefiGuiInputDequeue (&Ev) || (Ev.Type != UEFI_GUI_INPUT_KEY)) {
    return FALSE;
  }

  return (BOOLEAN)((Ev.u.Key.ScanCode == Scan) && (Ev.u.Key.UnicodeChar == Char) &&
                   (Ev.u.Key.Modifiers == Mods) && (Ev.u.Key.Pressed == Pressed));
}

/**
  Dequeue a press + release pair and compare both.
**/
STATIC
BOOLEAN
NextPair (
  IN UINT16  Scan,
  IN CHAR16  Char,
  IN UINT32  Mods
  )
{
  BOOLEAN  Press;
  BOOLEAN  Release;

  Press   = NextKey (Scan, Char, Mods, TRUE);
  Release = NextKey (Scan, Char, Mods, FALSE);
  return (BOOLEAN)(Press && Release);
}

STATIC
VOID
TestNoDevices (
  VOID
  )
{
  UEFI_GUI_INPUT_CONFIG  Config;
  EFI_EVENT              Events[UEFI_GUI_INPUT_MAX_WAIT_EVENTS];
  UINTN                  Count;

  MakeConfig (&Config, 64, 100);
  Check (L"init without devices", (BOOLEAN)(UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, NULL, NULL) == EFI_SUCCESS));
  Check (L"  no devices reported", (BOOLEAN)(UefiGuiInputGetDevices () == 0));
  Check (L"  poll succeeds", (BOOLEAN)(UefiGuiInputPoll () == EFI_SUCCESS));
  Count = UEFI_GUI_INPUT_MAX_WAIT_EVENTS;
  Check (L"  no wait events", (BOOLEAN)((UefiGuiInputGetWaitEvents (Events, &Count) == EFI_SUCCESS) && (Count == 0)));
  Check (L"  second init -> ALREADY_STARTED", (BOOLEAN)(UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, NULL, NULL) == EFI_ALREADY_STARTED));
  UefiGuiInputShutdown ();

  MakeConfig (&Config, 4, 100);
  Check (L"queue depth 4 rejected", (BOOLEAN)(UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, NULL, NULL) == EFI_INVALID_PARAMETER));
  MakeConfig (&Config, 64, 5);
  Check (L"speed 5% rejected", (BOOLEAN)(UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, NULL, NULL) == EFI_INVALID_PARAMETER));
  MakeConfig (&Config, 64, 100);
  Check (L"0 x 0 display rejected", (BOOLEAN)(UefiGuiInputInitializeWithProtocols (&Config, 0, 0, NULL, NULL, NULL, NULL) == EFI_INVALID_PARAMETER));
  Check (L"poll before init -> NOT_STARTED", (BOOLEAN)(UefiGuiInputPoll () == EFI_NOT_STARTED));
}

STATIC
VOID
TestKeyboardEx (
  VOID
  )
{
  UEFI_GUI_INPUT_CONFIG  Config;
  UEFI_GUI_INPUT_STATS   Stats;
  EFI_EVENT              Events[UEFI_GUI_INPUT_MAX_WAIT_EVENTS];
  UINTN                  Count;

  ResetFakes ();
  MakeConfig (&Config, 64, 100);
  UefiGuiInputInitializeWithProtocols (&Config, W, H, &mKbdEx.Protocol, &mKbd.Protocol, NULL, NULL);
  Check (L"Text Input Ex preferred over Simple Text Input", (BOOLEAN)(UefiGuiInputGetDevices () == UEFI_GUI_INPUT_DEVICE_KEYBOARD_EX));
  Count = UEFI_GUI_INPUT_MAX_WAIT_EVENTS;
  Check (
    L"  wait event is WaitForKeyEx",
    (BOOLEAN)((UefiGuiInputGetWaitEvents (Events, &Count) == EFI_SUCCESS) && (Count == 1) && (Events[0] == mKbdEx.Protocol.WaitForKeyEx))
    );

  FakeAddKeyEx (&mKbdEx, SCAN_NULL, L'a', NOMOD);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, L'A', SHIFT);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, L's', CTRL);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, L'C', CTRL | EFI_LEFT_SHIFT_PRESSED);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, 0x16, CTRL);                 // Ctrl+V as control char
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, L'x', ALT);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, CHAR_NULL, SHIFT);           // partial key stroke
  FakeAddKeyEx (&mKbdEx, SCAN_UP, CHAR_NULL, NOMOD);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, CHAR_TAB, SHIFT);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, CHAR_CARRIAGE_RETURN, NOMOD);
  FakeAddKeyEx (&mKbdEx, SCAN_NULL, L'q', EFI_LEFT_LOGO_PRESSED | EFI_SHIFT_STATE_VALID);
  Check (L"  poll", (BOOLEAN)(UefiGuiInputPoll () == EFI_SUCCESS));
  CheckValue (L"  10 keys -> 20 events (press + release)", (BOOLEAN)(UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_KEY) == 20), UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_KEY));
  Check (L"  'a'", NextPair (SCAN_NULL, L'a', 0));
  Check (L"  Shift+'A'", NextPair (SCAN_NULL, L'A', UEFI_GUI_KEY_MOD_SHIFT));
  Check (L"  Ctrl+s -> 's' + CTRL", NextPair (SCAN_NULL, L's', UEFI_GUI_KEY_MOD_CTRL));
  Check (L"  Ctrl+Shift+C -> 'c' + CTRL|SHIFT", NextPair (SCAN_NULL, L'c', UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_SHIFT));
  Check (L"  Ctrl+0x16 -> 'v' + CTRL", NextPair (SCAN_NULL, L'v', UEFI_GUI_KEY_MOD_CTRL));
  Check (L"  Alt+x", NextPair (SCAN_NULL, L'x', UEFI_GUI_KEY_MOD_ALT));
  Check (L"  partial key stroke skipped, Up next", NextPair (SCAN_UP, CHAR_NULL, 0));
  Check (L"  Shift+Tab", NextPair (SCAN_NULL, CHAR_TAB, UEFI_GUI_KEY_MOD_SHIFT));
  Check (L"  Enter", NextPair (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0));
  Check (L"  Logo+q", NextPair (SCAN_NULL, L'q', UEFI_GUI_KEY_MOD_LOGO));
  UefiGuiInputGetStats (&Stats);
  CheckValue (L"  key presses counted", (BOOLEAN)(Stats.KeyPresses == 10), Stats.KeyPresses);
  Check (L"  queue empty, nothing dropped", (BOOLEAN)((UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_ALL) == 0) && (Stats.DroppedEvents == 0)));
  UefiGuiInputShutdown ();
}

STATIC
VOID
TestKeyboardFallback (
  VOID
  )
{
  UEFI_GUI_INPUT_CONFIG  Config;

  ResetFakes ();
  MakeConfig (&Config, 64, 100);
  UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, &mKbd.Protocol, NULL, NULL);
  Check (L"Simple Text Input fallback", (BOOLEAN)(UefiGuiInputGetDevices () == UEFI_GUI_INPUT_DEVICE_KEYBOARD));
  FakeAddKey (&mKbd, SCAN_NULL, 0x13);
  FakeAddKey (&mKbd, SCAN_NULL, CHAR_BACKSPACE);
  FakeAddKey (&mKbd, SCAN_NULL, CHAR_TAB);
  FakeAddKey (&mKbd, SCAN_NULL, CHAR_CARRIAGE_RETURN);
  FakeAddKey (&mKbd, SCAN_ESC, CHAR_NULL);
  FakeAddKey (&mKbd, SCAN_NULL, L'z');
  UefiGuiInputPoll ();
  Check (L"  0x13 -> Ctrl+s", NextPair (SCAN_NULL, L's', UEFI_GUI_KEY_MOD_CTRL));
  Check (L"  0x08 stays Backspace", NextPair (SCAN_NULL, CHAR_BACKSPACE, 0));
  Check (L"  0x09 stays Tab", NextPair (SCAN_NULL, CHAR_TAB, 0));
  Check (L"  0x0D stays Enter", NextPair (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0));
  Check (L"  Esc", NextPair (SCAN_ESC, CHAR_NULL, 0));
  Check (L"  'z'", NextPair (SCAN_NULL, L'z', 0));
  UefiGuiInputShutdown ();

  MakeConfig (&Config, 64, 100);
  Config.EnableKeyboard = FALSE;
  UefiGuiInputInitializeWithProtocols (&Config, W, H, &mKbdEx.Protocol, &mKbd.Protocol, NULL, NULL);
  Check (L"EnableKeyboard = FALSE ignores keyboards", (BOOLEAN)(UefiGuiInputGetDevices () == 0));
  UefiGuiInputShutdown ();
}

STATIC
VOID
TestOverflow (
  VOID
  )
{
  UEFI_GUI_INPUT_CONFIG  Config;
  UEFI_GUI_INPUT_EVENT   Ev;
  UINTN                  Index;
  UINTN                  Presses;
  BOOLEAN                Paired;
  BOOLEAN                Expect;

  ResetFakes ();
  MakeConfig (&Config, 8, 100);
  UefiGuiInputInitializeWithProtocols (&Config, W, H, &mKbdEx.Protocol, NULL, NULL, NULL);
  for (Index = 0; Index < 6; Index++) {
    FakeAddKeyEx (&mKbdEx, SCAN_NULL, (CHAR16)(L'0' + Index), NOMOD);
  }

  UefiGuiInputPoll ();
  CheckValue (L"overflow: depth 8 keeps 4 keys (8 events)", (BOOLEAN)(UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_ALL) == 8), UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_ALL));
  CheckValue (L"  2 keys = 4 events dropped and counted", (BOOLEAN)(UefiGuiInputGetDroppedCount () == 4), (INT64)UefiGuiInputGetDroppedCount ());
  Paired  = TRUE;
  Presses = 0;
  Expect  = TRUE;
  while (UefiGuiInputDequeue (&Ev)) {
    if (Ev.u.Key.Pressed != Expect) {
      Paired = FALSE;
    }

    if (Ev.u.Key.Pressed) {
      if (Ev.u.Key.UnicodeChar != (CHAR16)(L'0' + Presses)) {
        Paired = FALSE;
      }

      Presses++;
    }

    Expect = (BOOLEAN)!Expect;
  }

  Check (L"  kept keys are the oldest, in order, each with its release", (BOOLEAN)(Paired && (Presses == 4)));
  UefiGuiInputShutdown ();
}

STATIC
VOID
TestKeyMapping (
  VOID
  )
{
  STATIC CONST struct {
    UINT16    Scan;
    CHAR16    Char;
    UINT32    Mods;
    UINT32    Expected;
    CHAR16    *Name;
  } Cases[] = {
    { SCAN_UP,        0,                    0,                      LV_KEY_UP,        L"Up"           },
    { SCAN_DOWN,      0,                    0,                      LV_KEY_DOWN,      L"Down"         },
    { SCAN_LEFT,      0,                    0,                      LV_KEY_LEFT,      L"Left"         },
    { SCAN_RIGHT,     0,                    0,                      LV_KEY_RIGHT,     L"Right"        },
    { SCAN_NULL,      CHAR_CARRIAGE_RETURN, 0,                      LV_KEY_ENTER,     L"Enter"        },
    { SCAN_ESC,       0,                    0,                      LV_KEY_ESC,       L"Escape"       },
    { SCAN_NULL,      CHAR_TAB,             0,                      LV_KEY_NEXT,      L"Tab"          },
    { SCAN_NULL,      CHAR_TAB,             UEFI_GUI_KEY_MOD_SHIFT, LV_KEY_PREV,      L"Shift+Tab"    },
    { SCAN_NULL,      CHAR_BACKSPACE,       0,                      LV_KEY_BACKSPACE, L"Backspace"    },
    { SCAN_DELETE,    0,                    0,                      LV_KEY_DEL,       L"Delete"       },
    { SCAN_HOME,      0,                    0,                      LV_KEY_HOME,      L"Home"         },
    { SCAN_END,       0,                    0,                      LV_KEY_END,       L"End"          },
    { SCAN_PAGE_UP,   0,                    0,                      0,                L"Page Up (none)" },
    { SCAN_PAGE_DOWN, 0,                    0,                      0,                L"Page Down (none)" },
    { SCAN_F1,        0,                    0,                      0,                L"F1 (none)"    },
    { SCAN_NULL,      L'a',                 UEFI_GUI_KEY_MOD_CTRL,  0,                L"Ctrl+A (shortcut)" },
    { SCAN_NULL,      L'c',                 UEFI_GUI_KEY_MOD_CTRL,  0,                L"Ctrl+C (shortcut)" },
    { SCAN_NULL,      L'v',                 UEFI_GUI_KEY_MOD_CTRL,  0,                L"Ctrl+V (shortcut)" },
    { SCAN_NULL,      L's',                 UEFI_GUI_KEY_MOD_CTRL,  0,                L"Ctrl+S (shortcut)" },
    { SCAN_NULL,      L'o',                 UEFI_GUI_KEY_MOD_CTRL,  0,                L"Ctrl+O (shortcut)" },
    { SCAN_NULL,      L'f',                 UEFI_GUI_KEY_MOD_CTRL,  0,                L"Ctrl+F (shortcut)" },
    { SCAN_NULL,      L'x',                 UEFI_GUI_KEY_MOD_ALT,   0,                L"Alt+X (shortcut)" },
    { SCAN_NULL,      L'a',                 0,                      'a',              L"'a'"          },
    { SCAN_NULL,      L'Z',                 UEFI_GUI_KEY_MOD_SHIFT, 'Z',              L"'Z'"          },
    { SCAN_NULL,      L' ',                 0,                      ' ',              L"space"        },
    { SCAN_NULL,      0x00E9,               0,                      0xA9C3,           L"U+00E9 -> UTF-8 C3 A9" },
    { SCAN_NULL,      0x20AC,               0,                      0xAC82E2,         L"U+20AC -> UTF-8 E2 82 AC" },
    { SCAN_NULL,      0x0001,               0,                      0,                L"stray control char (none)" },
  };
  UEFI_GUI_KEY_EVENT  Key;
  UINTN               Index;
  UINT32              Got;
  BOOLEAN             AllOk;

  AllOk = TRUE;
  for (Index = 0; Index < ARRAY_SIZE (Cases); Index++) {
    ZeroMem (&Key, sizeof (Key));
    Key.ScanCode    = Cases[Index].Scan;
    Key.UnicodeChar = Cases[Index].Char;
    Key.Modifiers   = Cases[Index].Mods;
    Key.Pressed     = TRUE;
    Got             = UefiGuiKeyToLvgl (&Key);
    if (Got != Cases[Index].Expected) {
      AllOk = FALSE;
      CheckValue (Cases[Index].Name, FALSE, Got);
    }
  }

  CheckValue (L"key mapping table (cases)", AllOk, (INT64)ARRAY_SIZE (Cases));
}

STATIC
VOID
TestRelativePointer (
  VOID
  )
{
  UEFI_GUI_INPUT_CONFIG  Config;
  UEFI_GUI_INPUT_EVENT   Ev;
  UEFI_GUI_INPUT_STATS   Stats;
  INT32                  X;
  INT32                  Y;
  UINT32                 Buttons;

  ResetFakes ();
  MakeConfig (&Config, 64, 100);
  mMouse.Mode.ResolutionX = 8;
  mMouse.Mode.ResolutionY = 8;
  UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, &mMouse.Protocol, NULL);
  Check (L"Simple Pointer in use", (BOOLEAN)(UefiGuiInputGetDevices () == UEFI_GUI_INPUT_DEVICE_SIMPLE_POINTER));
  UefiGuiInputGetPointerState (&X, &Y, &Buttons);
  Check (L"  cursor starts at the center", (BOOLEAN)((X == W / 2) && (Y == H / 2) && (Buttons == 0)));

  FakeAddRelative (&mMouse, 10, 5, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  Check (L"  res 8, 100%: +10,+5 counts -> +10,+5 px", (BOOLEAN)((X == W / 2 + 10) && (Y == H / 2 + 5)));
  Check (L"  one move event with the new position", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.Type == UEFI_GUI_INPUT_POINTER_MOVE) && (Ev.u.Move.X == X) && (Ev.u.Move.Y == Y)));

  mMouse.Mode.ResolutionX = 16;
  FakeAddRelative (&mMouse, 10, 0, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, NULL, NULL);
  Check (L"  res 16: +10 counts -> +5 px", (BOOLEAN)(X == W / 2 + 15));

  mMouse.Mode.ResolutionX = 0;
  FakeAddRelative (&mMouse, -7, 0, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, NULL, NULL);
  Check (L"  res 0 (unknown): -7 counts -> -7 px", (BOOLEAN)(X == W / 2 + 8));

  mMouse.Mode.ResolutionX = 8;
  FakeAddRelative (&mMouse, -100000, -100000, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  Check (L"  clamped at (0, 0)", (BOOLEAN)((X == 0) && (Y == 0)));
  FakeAddRelative (&mMouse, 100000, 100000, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  Check (L"  clamped at (W-1, H-1)", (BOOLEAN)((X == W - 1) && (Y == H - 1)));
  UefiGuiInputGetStats (&Stats);
  CheckValue (L"  queued moves merged (coalesced)", (BOOLEAN)(Stats.CoalescedMoves >= 2), (INT64)Stats.CoalescedMoves);
  while (UefiGuiInputDequeue (&Ev)) {
  }

  FakeAddRelative (&mMouse, 0, 0, 0, TRUE, FALSE);
  FakeAddRelative (&mMouse, 0, 0, 0, FALSE, FALSE);
  FakeAddRelative (&mMouse, 0, 0, 0, FALSE, TRUE);
  FakeAddRelative (&mMouse, 0, 0, 0, FALSE, FALSE);
  FakeAddRelative (&mMouse, 0, 0, 2, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputPoll ();
  UefiGuiInputPoll ();
  UefiGuiInputPoll ();
  UefiGuiInputPoll ();
  Check (L"  left press", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.Type == UEFI_GUI_INPUT_POINTER_BUTTON) && (Ev.u.Button.Button == UEFI_GUI_POINTER_BUTTON_LEFT) && Ev.u.Button.Pressed));
  Check (L"  left release", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.Type == UEFI_GUI_INPUT_POINTER_BUTTON) && (Ev.u.Button.Button == UEFI_GUI_POINTER_BUTTON_LEFT) && !Ev.u.Button.Pressed));
  Check (L"  right press", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.u.Button.Button == UEFI_GUI_POINTER_BUTTON_RIGHT) && Ev.u.Button.Pressed));
  Check (L"  right release", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.u.Button.Button == UEFI_GUI_POINTER_BUTTON_RIGHT) && !Ev.u.Button.Pressed));
  Check (L"  wheel Z=+2 (away) -> delta -2", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.Type == UEFI_GUI_INPUT_POINTER_WHEEL) && (Ev.u.Wheel.Delta == -2)));
  UefiGuiInputShutdown ();

  //
  // 50 %: half a pixel per count; the remainder must carry over.
  //
  ResetFakes ();
  MakeConfig (&Config, 64, 50);
  UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, &mMouse.Protocol, NULL);
  FakeAddRelative (&mMouse, 1, 0, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, NULL, NULL);
  Check (L"speed 50%: 1 count -> no pixel yet", (BOOLEAN)(X == W / 2));
  FakeAddRelative (&mMouse, 1, 0, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, NULL, NULL);
  Check (L"  2nd count -> 1 pixel (remainder kept)", (BOOLEAN)(X == W / 2 + 1));
  FakeAddRelative (&mMouse, -1, 0, 0, FALSE, FALSE);
  FakeAddRelative (&mMouse, -1, 0, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, NULL, NULL);
  Check (L"  2 counts back -> back to the start (symmetric)", (BOOLEAN)(X == W / 2));
  UefiGuiInputShutdown ();

  MakeConfig (&Config, 64, 200);
  ResetFakes ();
  UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, &mMouse.Protocol, NULL);
  FakeAddRelative (&mMouse, 3, -3, 0, FALSE, FALSE);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  Check (L"speed 200%: 3 counts -> 6 px", (BOOLEAN)((X == W / 2 + 6) && (Y == H / 2 - 6)));
  UefiGuiInputShutdown ();
}

STATIC
VOID
TestAbsolutePointer (
  VOID
  )
{
  UEFI_GUI_INPUT_CONFIG  Config;
  UEFI_GUI_INPUT_EVENT   Ev;
  INT32                  X;
  INT32                  Y;
  UINT32                 Buttons;

  ResetFakes ();
  MakeConfig (&Config, 64, 100);
  UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, NULL, &mTablet.Protocol);
  Check (L"Absolute Pointer in use", (BOOLEAN)(UefiGuiInputGetDevices () == UEFI_GUI_INPUT_DEVICE_ABSOLUTE_POINTER));
  FakeAddAbsolute (&mTablet, 0, 0, 0);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  Check (L"  (min, min) -> (0, 0)", (BOOLEAN)((X == 0) && (Y == 0)));
  FakeAddAbsolute (&mTablet, 1024, 1024, 0);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  Check (L"  (max, max) -> (W-1, H-1)", (BOOLEAN)((X == W - 1) && (Y == H - 1)));
  FakeAddAbsolute (&mTablet, 512, 256, 0);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  CheckValue (L"  (512, 256) of 1024 -> x 512", (BOOLEAN)(X == 512), X);
  CheckValue (L"  (512, 256) of 1024 -> y 192", (BOOLEAN)(Y == 192), Y);
  while (UefiGuiInputDequeue (&Ev)) {
  }

  FakeAddAbsolute (&mTablet, 512, 256, EFI_ABSP_TouchActive);
  FakeAddAbsolute (&mTablet, 512, 256, 0);
  FakeAddAbsolute (&mTablet, 512, 256, EFI_ABS_AltActive);
  FakeAddAbsolute (&mTablet, 512, 256, 0);
  UefiGuiInputPoll ();
  UefiGuiInputGetPointerState (NULL, NULL, &Buttons);
  Check (L"  touch -> left pressed", (BOOLEAN)(Buttons == UEFI_GUI_POINTER_BUTTON_LEFT));
  UefiGuiInputPoll ();
  UefiGuiInputPoll ();
  UefiGuiInputPoll ();
  Check (L"  same position: no move event, 4 button events", (BOOLEAN)(UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_POINTER) == 4));
  Check (L"  left press first", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.Type == UEFI_GUI_INPUT_POINTER_BUTTON) && (Ev.u.Button.Button == UEFI_GUI_POINTER_BUTTON_LEFT) && Ev.u.Button.Pressed));
  UefiGuiInputDequeue (&Ev);
  Check (L"  alt -> right press", (BOOLEAN)(UefiGuiInputDequeue (&Ev) && (Ev.u.Button.Button == UEFI_GUI_POINTER_BUTTON_RIGHT) && Ev.u.Button.Pressed));
  UefiGuiInputShutdown ();
}

STATIC
VOID
TestQueue (
  VOID
  )
{
  UEFI_GUI_INPUT_CONFIG  Config;
  UEFI_GUI_INPUT_EVENT   Ev;
  UINTN                  Index;
  BOOLEAN                Ordered;

  MakeConfig (&Config, 16, 100);
  UefiGuiInputInitializeWithProtocols (&Config, W, H, NULL, NULL, NULL, NULL);
  //
  // key0, move, key1, button, key2
  //
  for (Index = 0; Index < 3; Index++) {
    ZeroMem (&Ev, sizeof (Ev));
    Ev.Type              = UEFI_GUI_INPUT_KEY;
    Ev.u.Key.UnicodeChar = (CHAR16)(L'0' + Index);
    Ev.u.Key.Pressed     = TRUE;
    UefiGuiInputEnqueue (&Ev);
    ZeroMem (&Ev, sizeof (Ev));
    if (Index == 0) {
      Ev.Type     = UEFI_GUI_INPUT_POINTER_MOVE;
      Ev.u.Move.X = -50;
      Ev.u.Move.Y = 99999;
      UefiGuiInputEnqueue (&Ev);
    } else if (Index == 1) {
      Ev.Type             = UEFI_GUI_INPUT_POINTER_BUTTON;
      Ev.u.Button.Button  = UEFI_GUI_POINTER_BUTTON_LEFT;
      Ev.u.Button.Pressed = TRUE;
      UefiGuiInputEnqueue (&Ev);
    }
  }

  Check (L"queue: 3 keys and 2 pointer events", (BOOLEAN)((UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_KEY) == 3) && (UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_POINTER) == 2)));
  Check (L"  pointer kind first: clamped move", (BOOLEAN)(UefiGuiInputDequeueKind (UEFI_GUI_INPUT_KIND_POINTER, &Ev) && (Ev.Type == UEFI_GUI_INPUT_POINTER_MOVE) && (Ev.u.Move.X == 0) && (Ev.u.Move.Y == H - 1)));
  Check (L"  then the button", (BOOLEAN)(UefiGuiInputDequeueKind (UEFI_GUI_INPUT_KIND_POINTER, &Ev) && (Ev.Type == UEFI_GUI_INPUT_POINTER_BUTTON)));
  Ordered = TRUE;
  for (Index = 0; Index < 3; Index++) {
    if (!UefiGuiInputDequeue (&Ev) || (Ev.Type != UEFI_GUI_INPUT_KEY) || (Ev.u.Key.UnicodeChar != (CHAR16)(L'0' + Index))) {
      Ordered = FALSE;
    }
  }

  Check (L"  keys still in order", Ordered);
  Check (L"  timestamps set", (BOOLEAN)(Ev.TimestampMs != 0));
  ZeroMem (&Ev, sizeof (Ev));
  Ev.Type = (UEFI_GUI_INPUT_TYPE)42;
  Check (L"  invalid type rejected", (BOOLEAN)!UefiGuiInputEnqueue (&Ev));
  Ev.Type            = UEFI_GUI_INPUT_POINTER_BUTTON;
  Ev.u.Button.Button = BIT5;
  Check (L"  invalid button rejected", (BOOLEAN)!UefiGuiInputEnqueue (&Ev));
  UefiGuiInputShutdown ();
}

/**
  Run all unit tests.
**/
VOID
RunUnitTests (
  VOID
  )
{
  UEFI_GUI_MEMORY_STATS  Before;
  UEFI_GUI_MEMORY_STATS  After;

  if (EFI_ERROR (FakeDevicesInitialize (&mKbdEx, &mKbd, &mMouse, &mTablet))) {
    Check (L"fake devices", FALSE);
    return;
  }

  UefiGuiTimeInitialize ();
  UefiGuiGetMemoryStats (&Before);
  TestNoDevices ();
  TestKeyboardEx ();
  TestKeyboardFallback ();
  TestOverflow ();
  TestKeyMapping ();
  TestRelativePointer ();
  TestAbsolutePointer ();
  TestQueue ();
  UefiGuiGetMemoryStats (&After);
  Check (L"unit tests: all queue memory released", (BOOLEAN)(After.CurrentBytes == Before.CurrentBytes));
  FakeDevicesShutdown (&mKbdEx, &mKbd, &mMouse, &mTablet);
}
