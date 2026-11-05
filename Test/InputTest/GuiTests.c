/** @file
  InputTest GUI tests: real firmware keyboard and pointer through LVGL.

  The test drives itself: it writes the input it needs to the serial port as
  "UEFIGUI-CMD <seq> <cmd>[;<cmd>...]" (QEMU monitor syntax: sendkey,
  mouse_move, mouse_button), and scripts/qemu-driver.py performs it on the
  virtual USB keyboard and mouse. Each step then runs the GUI loop until
  LVGL shows the expected result or the step times out.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "InputTest.h"
#include <Protocol/SerialIo.h>

#define STEP_TIMEOUT_MS  8000

STATIC EFI_SERIAL_IO_PROTOCOL  *mSerial;
STATIC UINTN                   mSeq;
STATIC UINTN                   mKeysSent;

STATIC lv_obj_t  *mOk;
STATIC lv_obj_t  *mText;
STATIC lv_obj_t  *mCancel;
STATIC lv_obj_t  *mTarget;
STATIC lv_obj_t  *mList;
STATIC lv_obj_t  *mStatus;

STATIC UINTN   mOkClicks;
STATIC UINTN   mCancelClicks;
STATIC UINTN   mCancelEsc;
STATIC UINTN   mTargetClicks;
STATIC UINTN   mTargetRight;
STATIC UINTN   mShortcut[26];
STATIC INT32   mRightX;
STATIC INT32   mRightY;

typedef BOOLEAN (*CONDITION)(
  VOID  *Context
  );

/**
  Write one command line to the serial port.
**/
STATIC
VOID
SendCmd (
  IN CONST CHAR8  *Commands
  )
{
  CHAR8  Line[1024];
  UINTN  Size;

  mSeq++;
  AsciiSPrint (Line, sizeof (Line), "\r\nUEFIGUI-CMD %u %a\r\n", (UINT32)mSeq, Commands);
  Size = AsciiStrLen (Line);
  if (mSerial != NULL) {
    mSerial->Write (mSerial, &Size, Line);
  }
}

/**
  Count the sendkey commands in a command line.
**/
STATIC
UINTN
CountKeys (
  IN CONST CHAR8  *Commands
  )
{
  UINTN  Count;

  Count = 0;
  while ((Commands = AsciiStrStr (Commands, "sendkey")) != NULL) {
    Count++;
    Commands += 7;
  }

  return Count;
}

/**
  Run the GUI loop for Ms milliseconds.
**/
STATIC
VOID
Pump (
  IN UINT64  Ms
  )
{
  UINT64  End;

  End = UefiGuiGetTimeMs () + Ms;
  while (UefiGuiGetTimeMs () < End) {
    UefiGuiProcessEvents ();
  }
}

/**
  Send Commands, then run the loop until Condition holds or the step times
  out. A short settle time lets LVGL finish the frame.
**/
STATIC
BOOLEAN
Step (
  IN CONST CHAR8  *Commands  OPTIONAL,
  IN CONDITION    Condition,
  IN VOID         *Context
  )
{
  UINT64   End;
  BOOLEAN  Done;

  if (Commands != NULL) {
    mKeysSent += CountKeys (Commands);
    SendCmd (Commands);
  }

  End  = UefiGuiGetTimeMs () + STEP_TIMEOUT_MS;
  Done = FALSE;
  while (!Done && (UefiGuiGetTimeMs () < End)) {
    UefiGuiProcessEvents ();
    Done = Condition (Context);
  }

  Pump (100);
  return Done;
}

/**
  Run the loop until the cursor has not moved for 500 ms (the host may
  still be delivering a long movement in 127-count USB reports).
**/
STATIC
VOID
WaitPointerIdle (
  VOID
  )
{
  INT32   X;
  INT32   Y;
  INT32   LastX;
  INT32   LastY;
  UINT64  Since;
  UINT64  End;

  UefiGuiInputGetPointerState (&LastX, &LastY, NULL);
  Since = UefiGuiGetTimeMs ();
  End   = Since + STEP_TIMEOUT_MS;
  while ((UefiGuiGetTimeMs () < End) && (UefiGuiGetTimeMs () - Since < 500)) {
    UefiGuiProcessEvents ();
    UefiGuiInputGetPointerState (&X, &Y, NULL);
    if ((X != LastX) || (Y != LastY)) {
      LastX = X;
      LastY = Y;
      Since = UefiGuiGetTimeMs ();
    }
  }
}

/**
  Send a pointer move, wait until it is fully delivered, and report whether
  the cursor ended at Expected; the final position is recorded.
**/
STATIC
BOOLEAN
MoveTo (
  IN CONST CHAR16  *Name,
  IN CONST CHAR8   *Command,
  IN INT32         ExpectedX,
  IN INT32         ExpectedY
  )
{
  INT32    X;
  INT32    Y;
  BOOLEAN  Ok;

  SendCmd (Command);
  WaitPointerIdle ();
  Pump (100);
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  Ok = (BOOLEAN)((X == ExpectedX) && (Y == ExpectedY));
  CheckValue (Name, Ok, (INT64)X * 10000 + Y);
  return Ok;
}

//
// Conditions.
//
STATIC BOOLEAN CondOk (VOID *C) { return (BOOLEAN)(mOkClicks == (UINTN)C); }
STATIC BOOLEAN CondCancel (VOID *C) { return (BOOLEAN)(mCancelClicks == (UINTN)C); }
STATIC BOOLEAN CondEsc (VOID *C) { return (BOOLEAN)(mCancelEsc == (UINTN)C); }
STATIC BOOLEAN CondFocus (VOID *C) { return (BOOLEAN)(lv_group_get_focused (lv_group_get_default ()) == (lv_obj_t *)C); }
STATIC BOOLEAN CondText (VOID *C) { return (BOOLEAN)(AsciiStrCmp (lv_textarea_get_text (mText), (CONST CHAR8 *)C) == 0); }
STATIC BOOLEAN CondScrolled (VOID *C) { return (BOOLEAN)(lv_obj_get_scroll_y (mList) > 0); }
STATIC BOOLEAN CondTop (VOID *C) { return (BOOLEAN)(lv_obj_get_scroll_y (mList) == 0); }
STATIC BOOLEAN CondTargetClicks (VOID *C) { return (BOOLEAN)(mTargetClicks == (UINTN)C); }
STATIC BOOLEAN CondTargetRight (VOID *C) { return (BOOLEAN)(mTargetRight == (UINTN)C); }

STATIC
BOOLEAN
CondShortcuts (
  VOID  *C
  )
{
  CONST CHAR8  *Keys;

  for (Keys = (CONST CHAR8 *)C; *Keys != '\0'; Keys++) {
    if (mShortcut[*Keys - 'a'] == 0) {
      return FALSE;
    }
  }

  return TRUE;
}

typedef struct {
  INT32    X;
  INT32    Y;
} POS;

//
// Event handlers.
//
STATIC VOID OkClicked (lv_event_t *e) { mOkClicks++; lv_label_set_text_fmt (mStatus, "OK clicked (%u)", (unsigned)mOkClicks); }
STATIC VOID CancelClicked (lv_event_t *e) { mCancelClicks++; lv_label_set_text (mStatus, "Cancel clicked"); }
STATIC VOID TargetClicked (lv_event_t *e) { mTargetClicks++; lv_label_set_text_fmt (mStatus, "Target left click (%u)", (unsigned)mTargetClicks); }

STATIC
VOID
CancelKey (
  lv_event_t  *e
  )
{
  if (lv_event_get_key (e) == LV_KEY_ESC) {
    mCancelEsc++;
    lv_label_set_text (mStatus, "Escape on Cancel");
  }
}

STATIC
VOID
TargetRight (
  lv_event_t  *e
  )
{
  lv_point_t  *Point;

  Point = (lv_point_t *)lv_event_get_param (e);
  mTargetRight++;
  if (Point != NULL) {
    mRightX = Point->x;
    mRightY = Point->y;
  }

  lv_label_set_text_fmt (mStatus, "Target right click at %d,%d", (int)mRightX, (int)mRightY);
}

STATIC
BOOLEAN
EFIAPI
ShortcutHandler (
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN VOID                      *Context
  )
{
  if (((Key->Modifiers & UEFI_GUI_KEY_MOD_CTRL) != 0) && (Key->UnicodeChar >= L'a') && (Key->UnicodeChar <= L'z')) {
    mShortcut[Key->UnicodeChar - L'a']++;
    return TRUE;
  }

  return FALSE;
}

/**
  Build the test screen. Focus order (default group): OK, text, Cancel,
  Target, List.
**/
STATIC
VOID
BuildScreen (
  VOID
  )
{
  lv_obj_t  *Label;
  UINTN     Index;

  Label = lv_label_create (lv_screen_active ());
  lv_label_set_text (Label, "InputTest: keyboard and pointer through LVGL");
  lv_obj_set_style_text_font (Label, &lv_font_montserrat_20, 0);
  lv_obj_set_pos (Label, 100, 60);

  mOk = lv_button_create (lv_screen_active ());
  lv_obj_set_pos (mOk, 100, 150);
  lv_obj_set_size (mOk, 160, 50);
  lv_obj_add_event_cb (mOk, OkClicked, LV_EVENT_CLICKED, NULL);
  Label = lv_label_create (mOk);
  lv_label_set_text (Label, "OK");
  lv_obj_center (Label);

  mText = lv_textarea_create (lv_screen_active ());
  lv_textarea_set_one_line (mText, true);
  lv_obj_set_pos (mText, 300, 150);
  lv_obj_set_width (mText, 400);
  lv_textarea_set_placeholder_text (mText, "type here");

  mCancel = lv_button_create (lv_screen_active ());
  lv_obj_set_pos (mCancel, 740, 150);
  lv_obj_set_size (mCancel, 160, 50);
  lv_obj_add_event_cb (mCancel, CancelClicked, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb (mCancel, CancelKey, LV_EVENT_KEY, NULL);
  Label = lv_label_create (mCancel);
  lv_label_set_text (Label, "Cancel");
  lv_obj_center (Label);

  mTarget = lv_button_create (lv_screen_active ());
  lv_obj_set_pos (mTarget, 600, 300);
  lv_obj_set_size (mTarget, 300, 150);
  lv_obj_add_event_cb (mTarget, TargetClicked, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb (mTarget, TargetRight, (lv_event_code_t)UefiGuiGetRightClickEventCode (), NULL);
  Label = lv_label_create (mTarget);
  lv_label_set_text (Label, "Mouse target");
  lv_obj_center (Label);

  mList = lv_obj_create (lv_screen_active ());
  lv_obj_set_pos (mList, 100, 260);
  lv_obj_set_size (mList, 400, 200);
  lv_obj_set_flex_flow (mList, LV_FLEX_FLOW_COLUMN);
  for (Index = 0; Index < 30; Index++) {
    Label = lv_label_create (mList);
    lv_label_set_text_fmt (Label, "List line %u", (unsigned)Index);
  }

  lv_group_add_obj (lv_group_get_default (), mList);

  mStatus = lv_label_create (lv_screen_active ());
  lv_label_set_text (mStatus, "ready");
  lv_obj_set_style_text_font (mStatus, &lv_font_montserrat_20, 0);
  lv_obj_set_pos (mStatus, 100, 520);
}

/**
  Keyboard steps.
**/
STATIC
VOID
KeyboardTests (
  VOID
  )
{
  UEFI_GUI_INPUT_STATS  Stats;
  CHAR8                 Burst[600];
  CHAR8                 Expected[64];
  UINTN                 Index;
  int32_t               Page;

  Check (L"kbd: OK has the initial focus", CondFocus (mOk));
  Check (L"kbd: Enter clicks the focused button", Step ("sendkey ret", CondOk, (VOID *)1));
  Check (L"kbd: Tab moves focus to the text area", Step ("sendkey tab", CondFocus, mText));
  Check (L"kbd: typing 'hello'", Step ("sendkey h;sendkey e;sendkey l;sendkey l;sendkey o", CondText, "hello"));
  Check (L"kbd: Backspace", Step ("sendkey backspace", CondText, "hell"));
  Check (L"kbd: Left arrow then 'x'", Step ("sendkey left;sendkey x", CondText, "helxl"));
  Check (L"kbd: Home then 'y'", Step ("sendkey home;sendkey y", CondText, "yhelxl"));
  Check (L"kbd: End then 'z'", Step ("sendkey end;sendkey z", CondText, "yhelxlz"));
  Check (L"kbd: Home then Delete", Step ("sendkey home;sendkey delete", CondText, "helxlz"));

  //
  // Burst: 39 x 'a' and Shift+Q, one command every DRIVER_CMD_DELAY.
  //
  lv_textarea_set_text (mText, "");
  Burst[0] = '\0';
  for (Index = 0; Index < 39; Index++) {
    AsciiStrCatS (Burst, sizeof (Burst), "sendkey a;");
    Expected[Index] = 'a';
  }

  AsciiStrCatS (Burst, sizeof (Burst), "sendkey shift-q");
  Expected[39] = 'Q';
  Expected[40] = '\0';
  Check (L"kbd: 40-key burst arrives complete and in order", Step (Burst, CondText, Expected));
  CheckValue (L"kbd: burst length", (BOOLEAN)(AsciiStrLen (lv_textarea_get_text (mText)) == 40), (INT64)AsciiStrLen (lv_textarea_get_text (mText)));

  UefiGuiSetKeyHandler (ShortcutHandler, NULL);
  Check (
    L"kbd: Ctrl+A, C, V, S, O, F reach the key handler",
    Step ("sendkey ctrl-a;sendkey ctrl-c;sendkey ctrl-v;sendkey ctrl-s;sendkey ctrl-q;sendkey ctrl-o;sendkey ctrl-f", CondShortcuts, "acvsof")
    );
  Check (L"kbd: shortcuts typed nothing into the text area", CondText (Expected));

  Check (L"kbd: Tab to Cancel", Step ("sendkey tab", CondFocus, mCancel));
  Check (L"kbd: Enter clicks Cancel", Step ("sendkey ret", CondCancel, (VOID *)1));
  Check (L"kbd: Escape reaches the focused widget as LV_KEY_ESC", Step ("sendkey esc", CondEsc, (VOID *)1));
  Check (L"kbd: Shift+Tab moves focus back", Step ("sendkey shift-tab", CondFocus, mText));
  Check (L"kbd: Tab x3 to the list", Step ("sendkey tab;sendkey tab;sendkey tab", CondFocus, mList));
  Check (L"kbd: Page Down scrolls the focused list", Step ("sendkey pgdn", CondScrolled, NULL));
  Page = lv_obj_get_scroll_y (mList);
  CheckValue (L"kbd: scrolled by one page (px)", (BOOLEAN)(Page > 100), Page);
  Check (L"kbd: Page Up scrolls back to the top", Step ("sendkey pgup", CondTop, NULL));

  UefiGuiGetInputStats (&Stats);
  CheckValue (L"kbd: every key sent was read (no lost keys)", (BOOLEAN)(Stats.KeyPresses == mKeysSent), (INT64)Stats.KeyPresses);
  CheckValue (L"kbd: keys sent", TRUE, (INT64)mKeysSent);
  CheckValue (L"kbd: no events dropped", (BOOLEAN)(Stats.DroppedEvents == 0), (INT64)Stats.DroppedEvents);
  CheckValue (L"kbd: queue peak (events)", TRUE, Stats.QueuePeak);
}

/**
  Find the LVGL pointer input device.
**/
STATIC
lv_indev_t *
FindPointerIndev (
  VOID
  )
{
  lv_indev_t  *Indev;

  for (Indev = lv_indev_get_next (NULL); Indev != NULL; Indev = lv_indev_get_next (Indev)) {
    if (lv_indev_get_type (Indev) == LV_INDEV_TYPE_POINTER) {
      return Indev;
    }
  }

  return NULL;
}

/**
  Counts needed to move Pixels at 100 % speed on a Simple Pointer with
  Resolution counts per mm (see UefiGuiInputLib.h).
**/
STATIC
INT32
CountsFor (
  IN INT32   Pixels,
  IN UINT64  Resolution
  )
{
  if (Resolution == 0) {
    return Pixels;
  }

  return (INT32)DivS64x64Remainder ((INT64)Pixels * (INT64)Resolution, UEFI_GUI_POINTER_PIXELS_PER_MM, NULL);
}

STATIC
UINT32
ReadPixel (
  IN INT32  X,
  IN INT32  Y
  )
{
  UINT32  Value;

  if (EFI_ERROR (UefiGuiDisplayReadRect (&Value, (UINTN)X, (UINTN)Y, 1, 1))) {
    return 0xFFFFFFFF;
  }

  return Value;
}

/**
  Cursor, clicks and wheel with a relative (Simple Pointer) mouse.
**/
STATIC
VOID
RelativePointerTests (
  IN UINT32  Width,
  IN UINT32  Height
  )
{
  EFI_SIMPLE_POINTER_PROTOCOL  *Mouse;
  lv_indev_t                   *Indev;
  lv_point_t                   Point;
  POS                          Pos;
  CHAR8                        Cmd[128];
  UINT64                       Res;
  INT32                        X;
  INT32                        Y;
  EFI_HANDLE                   *Handles;
  UINTN                        Count;
  UINTN                        Index;

  //
  // The resolution of the physical mouse (not the console splitter, which
  // rescales to its own units): the host moves the physical device.
  //
  Res = 0;
  Handles = NULL;
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimplePointerProtocolGuid, NULL, &Count, &Handles))) {
    for (Index = 0; Index < Count; Index++) {
      if ((Handles[Index] != gST->ConsoleInHandle) &&
          !EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimplePointerProtocolGuid, (VOID **)&Mouse)) &&
          (Mouse->Mode != NULL))
      {
        Res = Mouse->Mode->ResolutionX;
        break;
      }
    }

    gBS->FreePool (Handles);
  }

  CheckValue (L"ptr: Simple Pointer resolution (counts/mm)", TRUE, (INT64)Res);
  Indev = FindPointerIndev ();
  Check (L"ptr: LVGL pointer input device exists", (BOOLEAN)(Indev != NULL));
  if (Indev == NULL) {
    return;
  }

  //
  // Positions are reported as x * 10000 + y.
  //
  MoveTo (L"ptr: large move up-left clamps at (0, 0)", "mouse_move -3000 -3000", 0, 0);
  lv_indev_get_point (Indev, &Point);
  Check (L"ptr: LVGL indev point is (0, 0)", (BOOLEAN)((Point.x == 0) && (Point.y == 0)));
  MoveTo (L"ptr: large move down-right clamps at (W-1, H-1)", "mouse_move 5000 5000", (INT32)Width - 1, (INT32)Height - 1);

  //
  // To the middle of the target: an exact position, so the scaling is
  // verified end to end.
  //
  Pos.X = 750;
  Pos.Y = 375;
  AsciiSPrint (Cmd, sizeof (Cmd), "mouse_move %d %d", CountsFor (Pos.X - (INT32)Width + 1, Res), CountsFor (Pos.Y - (INT32)Height + 1, Res));
  MoveTo (L"ptr: cursor moves exactly to the target (750, 375)", Cmd, Pos.X, Pos.Y);
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  lv_indev_get_point (Indev, &Point);
  Check (L"ptr: LVGL indev point matches", (BOOLEAN)((Point.x == 750) && (Point.y == 375)));
  Pump (200);
  CheckValue (L"ptr: cursor tip drawn (black outline at the hot spot)", (BOOLEAN)(ReadPixel (750, 375) == 0x000000), ReadPixel (750, 375));
  CheckValue (L"ptr: cursor fill drawn (white inside)", (BOOLEAN)(ReadPixel (751, 377) == 0xFFFFFF), ReadPixel (751, 377));
  SendCmd ("screenshot");
  Pump (2500);

  Check (L"ptr: left click on the target", Step ("mouse_button 1;mouse_button 0", CondTargetClicks, (VOID *)1));
  Check (L"ptr: right click on the target", Step ("mouse_button 2;mouse_button 0", CondTargetRight, (VOID *)1));
  Check (L"ptr: right click carries the cursor position", (BOOLEAN)((mRightX == 750) && (mRightY == 375)));
  Check (L"ptr: right click is not a left click", (BOOLEAN)(mTargetClicks == 1));

  //
  // Over the list, then one wheel notch toward the user.
  //
  Pos.X = 300;
  Pos.Y = 360;
  AsciiSPrint (Cmd, sizeof (Cmd), "mouse_move %d %d", CountsFor (300 - 750, Res), CountsFor (360 - 375, Res));
  MoveTo (L"ptr: move over the list (300, 360)", Cmd, Pos.X, Pos.Y);
  //
  // QEMU monitor: dz < 0 is a wheel turn down (toward the user).
  //
  Check (L"ptr: wheel down scrolls the list under the cursor", Step ("mouse_move 0 0 -1", CondScrolled, NULL));
  Pump (300);
  CheckValue (L"ptr: one notch scrolls 40 px", (BOOLEAN)(lv_obj_get_scroll_y (mList) == 40), lv_obj_get_scroll_y (mList));
  Check (L"ptr: wheel up scrolls back", Step ("mouse_move 0 0 1", CondTop, NULL));
}

/**
  Absolute-pointer counts that move the cursor by Pixels on an axis whose
  protocol range is [Min, Max] and display size is Size.
**/
STATIC
INT32
AbsCountsFor (
  IN INT32   Pixels,
  IN UINT64  Min,
  IN UINT64  Max,
  IN UINT32  Size
  )
{
  INT64  Num;

  Num = (INT64)Pixels * (INT64)(Max - Min);
  return (INT32)DivS64x64Remainder (Num + ((Num < 0) ? -(INT64)(Size - 1) / 2 : (INT64)(Size - 1) / 2), (INT64)(Size - 1), NULL);
}

/**
  Cursor and clicks with an Absolute Pointer device.

  QEMU has no absolute-pointer device that OVMF or MdeModulePkg can drive
  (usb-tablet is not a boot-protocol mouse), so CI uses usb-mouse with
  MdeModulePkg's UsbMouseAbsolutePointerDxe, which integrates the mouse's
  movement into an absolute position in [0, 1024]. That exercises the
  framework's whole Absolute Pointer path; positions go through two linear
  maps (device -> console splitter -> display), so the target position is
  checked to within 1 pixel. Corners are exact.
**/
STATIC
VOID
AbsolutePointerTests (
  IN UINT32  Width,
  IN UINT32  Height
  )
{
  EFI_ABSOLUTE_POINTER_PROTOCOL  *Abs;
  EFI_HANDLE                     *Handles;
  UINTN                          Count;
  UINTN                          Index;
  UINT64                         MinX;
  UINT64                         MaxX;
  UINT64                         MinY;
  UINT64                         MaxY;
  CHAR8                          Cmd[128];
  INT32                          X;
  INT32                          Y;

  MinX    = 0;
  MaxX    = 0;
  MinY    = 0;
  MaxY    = 0;
  Handles = NULL;
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL, &Count, &Handles))) {
    for (Index = 0; Index < Count; Index++) {
      if ((Handles[Index] != gST->ConsoleInHandle) &&
          !EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiAbsolutePointerProtocolGuid, (VOID **)&Abs)) &&
          (Abs->Mode != NULL))
      {
        MinX = Abs->Mode->AbsoluteMinX;
        MaxX = Abs->Mode->AbsoluteMaxX;
        MinY = Abs->Mode->AbsoluteMinY;
        MaxY = Abs->Mode->AbsoluteMaxY;
        break;
      }
    }

    gBS->FreePool (Handles);
  }

  CheckValue (L"abs: device X range max", (BOOLEAN)(MaxX > MinX), (INT64)MaxX);
  CheckValue (L"abs: device Y range max", (BOOLEAN)(MaxY > MinY), (INT64)MaxY);
  if ((MaxX <= MinX) || (MaxY <= MinY)) {
    return;
  }

  MoveTo (L"abs: move far up-left -> (0, 0)", "mouse_move -3000 -3000", 0, 0);
  MoveTo (L"abs: move far down-right -> (W-1, H-1)", "mouse_move 5000 5000", (INT32)Width - 1, (INT32)Height - 1);
  AsciiSPrint (
    Cmd,
    sizeof (Cmd),
    "mouse_move %d %d",
    AbsCountsFor (750 - ((INT32)Width - 1), MinX, MaxX, Width),
    AbsCountsFor (375 - ((INT32)Height - 1), MinY, MaxY, Height)
    );
  SendCmd (Cmd);
  WaitPointerIdle ();
  Pump (100);
  UefiGuiInputGetPointerState (&X, &Y, NULL);
  CheckValue (L"abs: cursor at the target (750, 375) +-1 px", (BOOLEAN)((ABS (X - 750) <= 1) && (ABS (Y - 375) <= 1)), (INT64)X * 10000 + Y);
  Check (L"abs: left click (touch active) on the target", Step ("mouse_button 1;mouse_button 0", CondTargetClicks, (VOID *)1));
  Check (L"abs: right click (alt active) on the target", Step ("mouse_button 2;mouse_button 0", CondTargetRight, (VOID *)1));
  Check (L"abs: right click is not a left click", (BOOLEAN)(mTargetClicks == 1));
}

/**
  Run the GUI tests.
**/
VOID
RunGuiTests (
  VOID
  )
{
  UEFI_GUI_CONFIG        Config;
  UEFI_GUI_DISPLAY_INFO  Info;
  UEFI_GUI_MEMORY_STATS  Before;
  UEFI_GUI_MEMORY_STATS  After;
  UINT32                 Devices;

  gBS->LocateProtocol (&gEfiSerialIoProtocolGuid, NULL, (VOID **)&mSerial);
  Check (L"serial port for host commands", (BOOLEAN)(mSerial != NULL));
  UefiGuiGetMemoryStats (&Before);

  UefiGuiConfigInitDefaults (&Config);
  if (EFI_ERROR (UefiGuiInitialize (&Config))) {
    Check (L"UefiGuiInitialize", FALSE);
    return;
  }

  UefiGuiGetDisplayInfo (&Info);
  Devices = UefiGuiGetInputDevices ();
  CheckValue (L"input devices (bits)", (BOOLEAN)((Devices & UEFI_GUI_INPUT_DEVICE_KEYBOARD_EX) != 0), Devices);
  BuildScreen ();
  Pump (300);

  KeyboardTests ();
  if ((Devices & UEFI_GUI_INPUT_DEVICE_SIMPLE_POINTER) != 0) {
    RelativePointerTests (Info.HorizontalResolution, Info.VerticalResolution);
  } else if ((Devices & UEFI_GUI_INPUT_DEVICE_ABSOLUTE_POINTER) != 0) {
    AbsolutePointerTests (Info.HorizontalResolution, Info.VerticalResolution);
  } else {
    Check (L"ptr: no pointer device (keyboard-only run)", TRUE);
  }

  UefiGuiSetKeyHandler (NULL, NULL);
  UefiGuiShutdown ();

  //
  // The UEFI Shell registers its own Ctrl+S notification (pause output):
  // the Ctrl+S sent above leaves the Shell's console halted, and its next
  // output waits for a key press. Provide that key press, as a user would.
  //
  SendCmd ("sendkey spc");
  gBS->Stall (1000000);
  gST->ConOut->ClearScreen (gST->ConOut);
  UefiGuiGetMemoryStats (&After);
  CheckValue (L"GUI session: all memory released (bytes left)", (BOOLEAN)(After.CurrentBytes == Before.CurrentBytes), (INT64)(After.CurrentBytes - Before.CurrentBytes));
}
