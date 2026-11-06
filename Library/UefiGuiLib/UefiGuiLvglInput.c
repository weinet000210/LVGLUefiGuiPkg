/** @file
  LVGL input devices: keypad (keyboard) and pointer (mouse / absolute
  pointer), fed from the UefiGuiInputLib queue in the GUI context.

  LVGL APIs used (checked against LVGL v9.6.0, include/lvgl/):
    lv_indev_create, lv_indev_set_type, lv_indev_set_read_cb,
    lv_indev_set_display, lv_indev_set_group, lv_indev_set_cursor,
    lv_indev_read, lv_indev_delete, lv_indev_search_obj, lv_group_create,
    lv_group_set_default, lv_group_get_focused, lv_group_delete,
    lv_event_register_id, lv_obj_scroll_by_bounded, lv_obj_is_scrollable,
    lv_obj_set_clickable.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInternal.h"

//
// Arrow cursor, 12 x 19: '#' outline (black), '.' fill (white).
//
#define CURSOR_W  12
#define CURSOR_H  19

STATIC CONST CHAR8  *mCursorShape[CURSOR_H] = {
  "#           ",
  "##          ",
  "#.#         ",
  "#..#        ",
  "#...#       ",
  "#....#      ",
  "#.....#     ",
  "#......#    ",
  "#.......#   ",
  "#........#  ",
  "#.........# ",
  "#......#####",
  "#...#..#    ",
  "#..# #..#   ",
  "#.#  #..#   ",
  "##    #..#  ",
  "#     #..#  ",
  "       #..# ",
  "       ###  ",
};

STATIC UINT32          mCursorPixels[CURSOR_W * CURSOR_H];
STATIC lv_image_dsc_t  mCursorDsc;

/**
  Pack a UCS-2 character as UTF-8 bytes in a uint32_t, first byte lowest,
  which is what LVGL's keypad and lv_textarea_add_char() expect.
**/
STATIC
UINT32
UefiGuiPackUtf8 (
  IN CHAR16  Char
  )
{
  if (Char < 0x80) {
    return Char;
  }

  if (Char < 0x800) {
    return (UINT32)(0xC0 | (Char >> 6)) | ((UINT32)(0x80 | (Char & 0x3F)) << 8);
  }

  if ((Char >= 0xD800) && (Char <= 0xDFFF)) {
    //
    // A lone UTF-16 surrogate is not a character.
    //
    return 0;
  }

  return (UINT32)(0xE0 | (Char >> 12)) |
         ((UINT32)(0x80 | ((Char >> 6) & 0x3F)) << 8) |
         ((UINT32)(0x80 | (Char & 0x3F)) << 16);
}

/**
  Map a key to the value the LVGL keypad reports, 0 for none.
**/
UINT32
EFIAPI
UefiGuiKeyToLvgl (
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if (Key == NULL) {
    return 0;
  }

  switch (Key->ScanCode) {
    case SCAN_UP:     return LV_KEY_UP;
    case SCAN_DOWN:   return LV_KEY_DOWN;
    case SCAN_LEFT:   return LV_KEY_LEFT;
    case SCAN_RIGHT:  return LV_KEY_RIGHT;
    case SCAN_HOME:   return LV_KEY_HOME;
    case SCAN_END:    return LV_KEY_END;
    case SCAN_DELETE: return LV_KEY_DEL;
    case SCAN_ESC:    return LV_KEY_ESC;
    case SCAN_NULL:   break;
    default:
      //
      // Page Up/Down, Insert, function keys: no LVGL key.
      //
      return 0;
  }

  //
  // Ctrl and Alt combinations are shortcuts, never text.
  //
  if ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) != 0) {
    return 0;
  }

  switch (Key->UnicodeChar) {
    case CHAR_CARRIAGE_RETURN:
    case CHAR_LINEFEED:
      return LV_KEY_ENTER;
    case CHAR_TAB:
      return ((Key->Modifiers & UEFI_GUI_KEY_MOD_SHIFT) != 0) ? LV_KEY_PREV : LV_KEY_NEXT;
    case CHAR_BACKSPACE:
      return LV_KEY_BACKSPACE;
    case 0x1B:
      return LV_KEY_ESC;
    case 0x7F:
      return LV_KEY_DEL;
    default:
      break;
  }

  if (Key->UnicodeChar < 0x20) {
    return 0;
  }

  return UefiGuiPackUtf8 (Key->UnicodeChar);
}

/**
  Page Up / Page Down: scroll the focused object, or its nearest scrollable
  parent, by one page.
**/
STATIC
VOID
UefiGuiPageScroll (
  IN BOOLEAN  Down
  )
{
  lv_group_t  *Group;
  lv_obj_t    *Obj;
  int32_t     Page;

  Group = lv_group_get_default ();
  Obj   = (Group != NULL) ? lv_group_get_focused (Group) : NULL;
  while ((Obj != NULL) && !lv_obj_is_scrollable (Obj)) {
    Obj = lv_obj_get_parent (Obj);
  }

  if (Obj == NULL) {
    return;
  }

  Page = lv_obj_get_content_height (Obj);
  //
  // lv_obj_scroll_by: positive dy moves the content down (shows what is
  // above), so Page Down scrolls by -Page.
  //
  lv_obj_scroll_by_bounded (Obj, 0, Down ? -Page : Page, LV_ANIM_OFF);
}

/**
  Keypad read callback. Each call reports one press or one release, and
  asks LVGL to call again while key events are queued, so a burst of keys
  is delivered in one indev cycle without loss.
**/
STATIC
void
UefiGuiKeypadRead (
  lv_indev_t       *indev,
  lv_indev_data_t  *data
  )
{
  UEFI_GUI_INPUT_EVENT  Event;
  UINT32                LvKey;

  while (UefiGuiInputDequeueKind (UEFI_GUI_INPUT_KIND_KEY, &Event)) {
    if (Event.u.Key.Pressed) {
      gUefiGui.KeyPresses++;
      if ((gUefiGui.KeyHandler != NULL) && gUefiGui.KeyHandler (&Event.u.Key, gUefiGui.KeyHandlerContext)) {
        gUefiGui.SkipKeyRelease = TRUE;
        continue;
      }

      LvKey = UefiGuiKeyToLvgl (&Event.u.Key);
      if (LvKey == 0) {
        if ((Event.u.Key.ScanCode == SCAN_PAGE_UP) || (Event.u.Key.ScanCode == SCAN_PAGE_DOWN)) {
          UefiGuiPageScroll ((BOOLEAN)(Event.u.Key.ScanCode == SCAN_PAGE_DOWN));
        }

        gUefiGui.SkipKeyRelease = TRUE;
        continue;
      }

      gUefiGui.SkipKeyRelease = FALSE;
      gUefiGui.LastLvKey      = LvKey;
      data->key               = LvKey;
      data->state             = LV_INDEV_STATE_PRESSED;
    } else {
      if (gUefiGui.SkipKeyRelease) {
        gUefiGui.SkipKeyRelease = FALSE;
        continue;
      }

      data->key   = gUefiGui.LastLvKey;
      data->state = LV_INDEV_STATE_RELEASED;
    }

    data->continue_reading = (bool)(UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_KEY) != 0);
    return;
  }

  data->key   = gUefiGui.LastLvKey;
  data->state = LV_INDEV_STATE_RELEASED;
}

/**
  The object under (X, Y) on the active screen and the top layer.
**/
STATIC
lv_obj_t *
UefiGuiObjectAt (
  IN INT32  X,
  IN INT32  Y
  )
{
  lv_point_t  Point;
  lv_obj_t    *Obj;

  Point.x = X;
  Point.y = Y;
  Obj     = lv_indev_search_obj (lv_layer_top (), &Point);
  if (Obj == NULL) {
    Obj = lv_indev_search_obj (lv_screen_active (), &Point);
  }

  return Obj;
}

/**
  Pointer read callback. Moves and left button changes are reported one per
  call (LVGL must see every press and release); right clicks and the wheel
  are handled here directly.
**/
STATIC
void
UefiGuiPointerRead (
  lv_indev_t       *indev,
  lv_indev_data_t  *data
  )
{
  UEFI_GUI_INPUT_EVENT  Event;
  lv_obj_t              *Obj;
  lv_point_t            Point;
  BOOLEAN               Report;

  Report = FALSE;
  while (!Report && UefiGuiInputDequeueKind (UEFI_GUI_INPUT_KIND_POINTER, &Event)) {
    switch (Event.Type) {
      case UEFI_GUI_INPUT_POINTER_MOVE:
        gUefiGui.PointerX = Event.u.Move.X;
        gUefiGui.PointerY = Event.u.Move.Y;
        Report            = TRUE;
        break;

      case UEFI_GUI_INPUT_POINTER_BUTTON:
        if (Event.u.Button.Button == UEFI_GUI_POINTER_BUTTON_LEFT) {
          gUefiGui.PointerLeft = Event.u.Button.Pressed;
          Report               = TRUE;
        } else if (Event.u.Button.Button == UEFI_GUI_POINTER_BUTTON_RIGHT) {
          if (Event.u.Button.Pressed) {
            gUefiGui.RightPressObj = UefiGuiObjectAt (gUefiGui.PointerX, gUefiGui.PointerY);
          } else {
            Obj = UefiGuiObjectAt (gUefiGui.PointerX, gUefiGui.PointerY);
            //
            // A right click is a press and a release on the same object.
            //
            if ((Obj != NULL) && (Obj == gUefiGui.RightPressObj)) {
              Point.x = gUefiGui.PointerX;
              Point.y = gUefiGui.PointerY;
              lv_obj_send_event (Obj, (lv_event_code_t)gUefiGui.RightClickEvent, &Point);
            }

            gUefiGui.RightPressObj = NULL;
          }
        }

        break;

      case UEFI_GUI_INPUT_POINTER_WHEEL:
        Obj = UefiGuiObjectAt (gUefiGui.PointerX, gUefiGui.PointerY);
        while ((Obj != NULL) && !lv_obj_is_scrollable (Obj)) {
          Obj = lv_obj_get_parent (Obj);
        }

        if (Obj != NULL) {
          //
          // Positive delta (toward the user) shows content further down.
          //
          lv_obj_scroll_by_bounded (Obj, 0, -Event.u.Wheel.Delta * UEFI_GUI_WHEEL_STEP_PX, LV_ANIM_OFF);
        }

        break;

      default:
        break;
    }
  }

  data->point.x          = gUefiGui.PointerX;
  data->point.y          = gUefiGui.PointerY;
  data->state            = gUefiGui.PointerLeft ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->continue_reading = (bool)(Report && (UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_POINTER) != 0));
}

/**
  Build the arrow cursor image.
**/
STATIC
VOID
UefiGuiBuildCursor (
  VOID
  )
{
  UINTN  X;
  UINTN  Y;

  for (Y = 0; Y < CURSOR_H; Y++) {
    for (X = 0; X < CURSOR_W; X++) {
      switch (mCursorShape[Y][X]) {
        case '#':
          mCursorPixels[Y * CURSOR_W + X] = 0xFF000000;
          break;
        case '.':
          mCursorPixels[Y * CURSOR_W + X] = 0xFFFFFFFF;
          break;
        default:
          mCursorPixels[Y * CURSOR_W + X] = 0x00000000;
          break;
      }
    }
  }

  ZeroMem (&mCursorDsc, sizeof (mCursorDsc));
  mCursorDsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
  mCursorDsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
  mCursorDsc.header.w      = CURSOR_W;
  mCursorDsc.header.h      = CURSOR_H;
  mCursorDsc.header.stride = CURSOR_W * 4;
  mCursorDsc.data_size     = sizeof (mCursorPixels);
  mCursorDsc.data          = (CONST UINT8 *)mCursorPixels;
}

/**
  Discover input devices and create the LVGL input devices.
**/
EFI_STATUS
UefiGuiLvglInputInitialize (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      Devices;
  INT32       X;
  INT32       Y;

  Status = UefiGuiInputInitialize (
             &gUefiGui.Config.Input,
             (UINT32)lv_display_get_horizontal_resolution (gUefiGui.Display),
             (UINT32)lv_display_get_vertical_resolution (gUefiGui.Display)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gUefiGui.InputInitialized = TRUE;
  gUefiGui.RightClickEvent  = lv_event_register_id ();
  Devices                   = UefiGuiInputGetDevices ();

  //
  // Default group: focusable widgets join it when they are created.
  //
  gUefiGui.Group = lv_group_create ();
  if (gUefiGui.Group == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  lv_group_set_default (gUefiGui.Group);

  if ((Devices & (UEFI_GUI_INPUT_DEVICE_KEYBOARD | UEFI_GUI_INPUT_DEVICE_KEYBOARD_EX)) != 0) {
    gUefiGui.Keypad = lv_indev_create ();
    if (gUefiGui.Keypad == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    lv_indev_set_type (gUefiGui.Keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb (gUefiGui.Keypad, UefiGuiKeypadRead);
    lv_indev_set_display (gUefiGui.Keypad, gUefiGui.Display);
    lv_indev_set_group (gUefiGui.Keypad, gUefiGui.Group);
  } else {
    UefiGuiRecordError ("no keyboard", EFI_NOT_FOUND);
  }

  if ((Devices & (UEFI_GUI_INPUT_DEVICE_SIMPLE_POINTER | UEFI_GUI_INPUT_DEVICE_ABSOLUTE_POINTER)) != 0) {
    UefiGuiInputGetPointerState (&X, &Y, NULL);
    gUefiGui.PointerX = X;
    gUefiGui.PointerY = Y;
    gUefiGui.Pointer  = lv_indev_create ();
    if (gUefiGui.Pointer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    lv_indev_set_type (gUefiGui.Pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb (gUefiGui.Pointer, UefiGuiPointerRead);
    lv_indev_set_display (gUefiGui.Pointer, gUefiGui.Display);
    if (gUefiGui.Config.Input.ShowCursor) {
      UefiGuiBuildCursor ();
      gUefiGui.Cursor = lv_image_create (lv_layer_sys ());
      if (gUefiGui.Cursor == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }

      lv_image_set_src (gUefiGui.Cursor, &mCursorDsc);
      lv_obj_set_clickable (gUefiGui.Cursor, false);
      lv_indev_set_cursor (gUefiGui.Pointer, gUefiGui.Cursor);
    }
  }

  return EFI_SUCCESS;
}

/**
  Poll the devices; let LVGL read new input right away instead of waiting
  for its next indev period.
**/
VOID
UefiGuiLvglInputProcess (
  VOID
  )
{
  if (!gUefiGui.InputInitialized) {
    return;
  }

  if (EFI_ERROR (UefiGuiInputPoll ())) {
    UefiGuiRecordError ("input device", EFI_DEVICE_ERROR);
  }

  if ((gUefiGui.Keypad != NULL) && (UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_KEY) != 0)) {
    lv_indev_read (gUefiGui.Keypad);
  }

  if ((gUefiGui.Pointer != NULL) && (UefiGuiInputPendingKind (UEFI_GUI_INPUT_KIND_POINTER) != 0)) {
    lv_indev_read (gUefiGui.Pointer);
  }
}

/**
  Delete the LVGL input devices and the group; release the input queue.
**/
VOID
UefiGuiLvglInputShutdown (
  VOID
  )
{
  if (gUefiGui.Pointer != NULL) {
    lv_indev_delete (gUefiGui.Pointer);
    gUefiGui.Pointer = NULL;
  }

  if (gUefiGui.Keypad != NULL) {
    lv_indev_delete (gUefiGui.Keypad);
    gUefiGui.Keypad = NULL;
  }

  //
  // The cursor lives on the system layer and is deleted with the display.
  //
  gUefiGui.Cursor = NULL;
  if (gUefiGui.Group != NULL) {
    if (lv_group_get_default () == gUefiGui.Group) {
      lv_group_set_default (NULL);
    }

    lv_group_delete (gUefiGui.Group);
    gUefiGui.Group = NULL;
  }

  if (gUefiGui.InputInitialized) {
    UefiGuiInputShutdown ();
    gUefiGui.InputInitialized = FALSE;
  }
}

/**
  Install the application key handler.
**/
VOID
EFIAPI
UefiGuiSetKeyHandler (
  IN UEFI_GUI_KEY_HANDLER  Handler  OPTIONAL,
  IN VOID                  *Context OPTIONAL
  )
{
  gUefiGui.KeyHandler        = Handler;
  gUefiGui.KeyHandlerContext = Context;
}

/**
  Event code of right clicks.
**/
UINT32
EFIAPI
UefiGuiGetRightClickEventCode (
  VOID
  )
{
  return gUefiGui.RightClickEvent;
}

/**
  Input statistics.
**/
EFI_STATUS
EFIAPI
UefiGuiGetInputStats (
  OUT UEFI_GUI_INPUT_STATS  *Stats
  )
{
  if (Stats == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGui.Initialized || !gUefiGui.InputInitialized) {
    return EFI_NOT_STARTED;
  }

  return UefiGuiInputGetStats (Stats);
}
