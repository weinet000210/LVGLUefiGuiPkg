/** @file
  Keyboard backend: Simple Text Input Ex, with Simple Text Input fallback.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInputInternal.h"

/**
  Translate EFI shift state bits to UEFI_GUI_KEY_MOD_* flags.
**/
STATIC
UINT32
UefiGuiKeyModifiers (
  IN UINT32  ShiftState
  )
{
  UINT32  Modifiers;

  Modifiers = 0;
  if ((ShiftState & EFI_SHIFT_STATE_VALID) == 0) {
    return 0;
  }

  if ((ShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) != 0) {
    Modifiers |= UEFI_GUI_KEY_MOD_SHIFT;
  }

  if ((ShiftState & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) != 0) {
    Modifiers |= UEFI_GUI_KEY_MOD_CTRL;
  }

  if ((ShiftState & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) != 0) {
    Modifiers |= UEFI_GUI_KEY_MOD_ALT;
  }

  if ((ShiftState & (EFI_LEFT_LOGO_PRESSED | EFI_RIGHT_LOGO_PRESSED)) != 0) {
    Modifiers |= UEFI_GUI_KEY_MOD_LOGO;
  }

  return Modifiers;
}

/**
  Normalize one key stroke and queue it as a press plus a release.

  @param[in]  Key          Key as read.
  @param[in]  Modifiers    UEFI_GUI_KEY_MOD_* flags.
  @param[in]  ModsKnown    TRUE when the firmware reported the shift state.
**/
STATIC
VOID
UefiGuiQueueKey (
  IN EFI_INPUT_KEY  Key,
  IN UINT32         Modifiers,
  IN BOOLEAN        ModsKnown
  )
{
  UEFI_GUI_INPUT_EVENT  Event;
  CHAR16                Char;

  Char = Key.UnicodeChar;

  //
  // Ctrl+letter: Text Input Ex reports the letter plus the Ctrl state,
  // Simple Text Input the control character 0x01..0x1A. Normalize both to
  // the lower case letter with UEFI_GUI_KEY_MOD_CTRL.
  //
  if ((Modifiers & UEFI_GUI_KEY_MOD_CTRL) != 0) {
    if ((Char >= L'A') && (Char <= L'Z')) {
      Char = (CHAR16)(Char - L'A' + L'a');
    } else if ((Char >= 0x01) && (Char <= 0x1A)) {
      Char = (CHAR16)(Char - 1 + L'a');
    }
  } else if (!ModsKnown && (Char >= 0x01) && (Char <= 0x1A) &&
             (Char != CHAR_BACKSPACE) && (Char != CHAR_TAB) &&
             (Char != CHAR_LINEFEED) && (Char != CHAR_CARRIAGE_RETURN))
  {
    Char       = (CHAR16)(Char - 1 + L'a');
    Modifiers |= UEFI_GUI_KEY_MOD_CTRL;
  }

  //
  // A press and its synthesized release are queued together or not at all.
  //
  if (UefiGuiInputFreeSlots () < 2) {
    gUefiGuiInput.Stats.DroppedEvents += 2;
    return;
  }

  ZeroMem (&Event, sizeof (Event));
  Event.Type                = UEFI_GUI_INPUT_KEY;
  Event.u.Key.ScanCode      = Key.ScanCode;
  Event.u.Key.UnicodeChar   = Char;
  Event.u.Key.Modifiers     = Modifiers;
  Event.u.Key.Pressed       = TRUE;
  UefiGuiInputQueueAppend (&Event);
  Event.u.Key.Pressed       = FALSE;
  UefiGuiInputQueueAppend (&Event);
}

/**
  Read every pending key stroke.
**/
EFI_STATUS
UefiGuiKeyboardPoll (
  VOID
  )
{
  EFI_STATUS     Status;
  EFI_KEY_DATA   KeyData;
  EFI_INPUT_KEY  Key;
  UINTN          Index;

  for (Index = 0; Index < UEFI_GUI_MAX_KEYS_PER_POLL; Index++) {
    if (gUefiGuiInput.TextInEx != NULL) {
      ZeroMem (&KeyData, sizeof (KeyData));
      Status = gUefiGuiInput.TextInEx->ReadKeyStrokeEx (gUefiGuiInput.TextInEx, &KeyData);
      if (Status == EFI_NOT_READY) {
        return EFI_SUCCESS;
      }

      if (EFI_ERROR (Status)) {
        gUefiGuiInput.Stats.DeviceErrors++;
        return EFI_DEVICE_ERROR;
      }

      //
      // A partial key stroke (only a shift or toggle state changed) carries
      // no key.
      //
      if ((KeyData.Key.ScanCode == SCAN_NULL) && (KeyData.Key.UnicodeChar == CHAR_NULL)) {
        continue;
      }

      gUefiGuiInput.Stats.KeyPresses++;
      UefiGuiQueueKey (
        KeyData.Key,
        UefiGuiKeyModifiers (KeyData.KeyState.KeyShiftState),
        (BOOLEAN)((KeyData.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) != 0)
        );
    } else if (gUefiGuiInput.TextIn != NULL) {
      Status = gUefiGuiInput.TextIn->ReadKeyStroke (gUefiGuiInput.TextIn, &Key);
      if (Status == EFI_NOT_READY) {
        return EFI_SUCCESS;
      }

      if (EFI_ERROR (Status)) {
        gUefiGuiInput.Stats.DeviceErrors++;
        return EFI_DEVICE_ERROR;
      }

      if ((Key.ScanCode == SCAN_NULL) && (Key.UnicodeChar == CHAR_NULL)) {
        continue;
      }

      gUefiGuiInput.Stats.KeyPresses++;
      UefiGuiQueueKey (Key, 0, FALSE);
    } else {
      return EFI_SUCCESS;
    }
  }

  return EFI_SUCCESS;
}
