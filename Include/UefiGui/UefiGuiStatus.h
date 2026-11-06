/** @file
  Error reporting helpers shared by the UefiGui libraries.

  UefiGui reports failures with standard EFI_STATUS values. Each backend also
  records the most recent failure with the component that produced it, so an
  application can call UefiGuiGetLastError() after a call that degraded
  gracefully (for example a missing mouse) instead of failing.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_STATUS_H_
#define UEFI_GUI_STATUS_H_

#include <Uefi.h>
#include <Library/DebugLib.h>

///
/// Component that produced an error.
///
typedef enum {
  UefiGuiComponentNone = 0,
  UefiGuiComponentCore,
  UefiGuiComponentDisplay,
  UefiGuiComponentKeyboard,
  UefiGuiComponentPointer,
  UefiGuiComponentTimer,
  UefiGuiComponentMemory,
  UefiGuiComponentLvgl,
  UefiGuiComponentMax
} UEFI_GUI_COMPONENT;

///
/// Debug print level used by all UefiGui messages.
///
#define UEFI_GUI_DEBUG_INFO   DEBUG_INFO
#define UEFI_GUI_DEBUG_ERROR  DEBUG_ERROR

///
/// Print an error tagged with the calling function.
/// Usage:  UEFI_GUI_LOG_ERROR ("GOP Blt", Status);
///
#define UEFI_GUI_LOG_ERROR(What, Status)                                     \
  DEBUG ((UEFI_GUI_DEBUG_ERROR, "[UefiGui] %a: %a failed: %r\n",           \
          __func__, (What), (Status)))

#endif // UEFI_GUI_STATUS_H_
