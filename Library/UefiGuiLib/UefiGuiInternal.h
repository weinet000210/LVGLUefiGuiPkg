/** @file
  Internal state of UefiGuiLib.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_INTERNAL_H_
#define UEFI_GUI_INTERNAL_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/LvglLib.h>
#include <Library/UefiGuiDisplayLib.h>
#include <Library/UefiGuiInputLib.h>
#include <Library/UefiGuiLib.h>
#include <Library/UefiGuiRuntimeLib.h>

#define UEFI_GUI_DEFAULT_DRAW_LINES   64
#define UEFI_GUI_MAX_DRAW_LINES       4096
#define UEFI_GUI_DEFAULT_MIN_WAIT_MS  1
#define UEFI_GUI_DEFAULT_MAX_WAIT_MS  50
#define UEFI_GUI_MAX_WAIT_LIMIT_MS    1000
#define UEFI_GUI_WHEEL_STEP_PX        40

typedef struct {
  BOOLEAN                  Initialized;
  BOOLEAN                  LvglInitialized;
  BOOLEAN                  ExitRequested;
  UEFI_GUI_CONFIG          Config;
  lv_display_t             *Display;
  VOID                     *DrawBuffer[2];
  UINT32                   DrawBufferSize;
  EFI_EVENT                WaitTimer;
  EFI_STATUS               LastError;
  UEFI_GUI_RENDER_STATS    Stats;
  UINT32                   InjectedFlushFailures;

  //
  // Input (UefiGuiLvglInput.c).
  //
  BOOLEAN                  InputInitialized;
  lv_group_t               *Group;
  lv_indev_t               *Keypad;
  lv_indev_t               *Pointer;
  lv_obj_t                 *Cursor;
  UEFI_GUI_KEY_HANDLER     KeyHandler;
  VOID                     *KeyHandlerContext;
  UINT32                   LastLvKey;
  BOOLEAN                  SkipKeyRelease;
  UINT64                   KeyPresses;
  INT32                    PointerX;
  INT32                    PointerY;
  BOOLEAN                  PointerLeft;
  lv_obj_t                 *RightPressObj;
  UINT32                   RightClickEvent;
} UEFI_GUI_CONTEXT;

extern UEFI_GUI_CONTEXT  gUefiGui;

/**
  Remember Status as the last error (if it is an error) and report it.
**/
VOID
UefiGuiRecordError (
  IN CONST CHAR8  *What,
  IN EFI_STATUS   Status
  );

//
// UefiGuiLvglPort.c
//

/**
  lv_init(), hooks, display creation and draw buffers.
**/
EFI_STATUS
UefiGuiLvglInitialize (
  VOID
  );

/**
  Delete the display, lv_deinit(), free the draw buffers.
**/
VOID
UefiGuiLvglShutdown (
  VOID
  );

//
// UefiGuiLvglInput.c
//

EFI_STATUS
UefiGuiLvglInputInitialize (
  VOID
  );

VOID
UefiGuiLvglInputProcess (
  VOID
  );

VOID
UefiGuiLvglInputShutdown (
  VOID
  );

//
// UefiGuiEventLoop.c
//

EFI_STATUS
UefiGuiEventLoopInitialize (
  VOID
  );

VOID
UefiGuiEventLoopShutdown (
  VOID
  );

#endif // UEFI_GUI_INTERNAL_H_
