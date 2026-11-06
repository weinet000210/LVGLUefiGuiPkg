/** @file
  UefiGuiLib - public API of the UefiGui framework.

  These are the only UefiGui functions a typical application calls; it never
  touches GOP, input protocols or the LVGL display/indev setup directly.

  What initializes LVGL: UefiGuiInitialize() calls lv_init(), installs the
  tick, log, memory and assert hooks, creates the LVGL display (partial
  rendering, XRGB8888 draw buffers) and connects it to UefiGuiDisplayLib.
  UefiGuiShutdown() deletes the display and calls lv_deinit().
  What the application calls: everything else in this header, plus LVGL
  widget APIs (lv_label_create(), lv_button_create(), ...) on the active
  screen.

  Status by phase: display, LVGL, time base and the timer-driven loop are
  implemented (Phase 2). Keyboard and pointer input arrive in Phases 3 and 4;
  until then UefiGuiGetInputDevices() reports no devices and the loop waits
  on its timer only.

  Call order:
    UefiGuiConfigInitDefaults (&Config);   // application, optional overrides
    UefiGuiInitialize (&Config);           // initializes LVGL, display, input
    ... build widgets with LVGL APIs ...
    UefiGuiRun ();                         // returns after UefiGuiRequestExit()
    UefiGuiShutdown ();                    // releases everything, deinits LVGL

  Applications with their own loop call UefiGuiProcessEvents() repeatedly
  instead of UefiGuiRun().

  All functions must be called from the application's main execution context
  at TPL_APPLICATION. LVGL APIs may only be called from that same context.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_LIB_H_
#define UEFI_GUI_LIB_H_

#include <UefiGui/UefiGuiTypes.h>
#include <UefiGui/UefiGuiConfig.h>
#include <UefiGui/UefiGuiStatus.h>

/**
  Fill Config with the documented defaults.
**/
VOID
EFIAPI
UefiGuiConfigInitDefaults (
  OUT UEFI_GUI_CONFIG  *Config
  );

/**
  Initialize LVGL and all UefiGui backends.

  @param[in]  Config   Options, or NULL for defaults.

  @retval EFI_SUCCESS            Ready. Missing input devices are tolerated;
                                 see UefiGuiGetLastError().
  @retval EFI_ALREADY_STARTED    Already initialized.
  @retval EFI_INCOMPATIBLE_VERSION  Config->Revision is not supported.
  @retval EFI_NOT_FOUND          No GOP.
  @retval other                  Backend failure; nothing is left allocated.
**/
EFI_STATUS
EFIAPI
UefiGuiInitialize (
  IN UEFI_GUI_CONFIG  *Config  OPTIONAL
  );

/**
  Run the event loop until UefiGuiRequestExit() is called.
**/
EFI_STATUS
EFIAPI
UefiGuiRun (
  VOID
  );

/**
  Process pending input and due LVGL timers once, sleeping at most the time
  until the next LVGL timer is due.

  @retval EFI_SUCCESS   Processed.
  @retval EFI_ABORTED   An exit was requested.
**/
EFI_STATUS
EFIAPI
UefiGuiProcessEvents (
  VOID
  );

/**
  Describe the active display.
**/
EFI_STATUS
EFIAPI
UefiGuiGetDisplayInfo (
  OUT UEFI_GUI_DISPLAY_INFO  *Info
  );

/**
  Return UEFI_GUI_INPUT_DEVICE_* bits for the input devices in use.
  Always 0 until the input backends (Phases 3 and 4) are connected.
**/
UINT32
EFIAPI
UefiGuiGetInputDevices (
  VOID
  );

/**
  Ask UefiGuiRun() to return after the current iteration. Safe to call from
  LVGL event handlers.
**/
EFI_STATUS
EFIAPI
UefiGuiRequestExit (
  VOID
  );

/**
  Return the most recent non-fatal or fatal error recorded by any backend.

  @param[out]  Status     Last error, EFI_SUCCESS if none.
**/
EFI_STATUS
EFIAPI
UefiGuiGetLastError (
  OUT EFI_STATUS  *Status
  );

/**
  Stop timers, close events, release backends and deinitialize LVGL. Safe to
  call more than once and after a failed UefiGuiInitialize().
**/
VOID
EFIAPI
UefiGuiShutdown (
  VOID
  );

/**
  Read the rendering statistics.

  @retval EFI_SUCCESS            Stats filled.
  @retval EFI_INVALID_PARAMETER  Stats is NULL.
  @retval EFI_NOT_STARTED        Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiGetRenderStats (
  OUT UEFI_GUI_RENDER_STATS  *Stats
  );

/**
  Debug and test aid: make the next Count flushes fail with
  EFI_DEVICE_ERROR before they reach the display, to exercise the error path
  (LVGL is still told the flush finished). 0 cancels.
**/
VOID
EFIAPI
UefiGuiDebugInjectFlushFailures (
  IN UINT32  Count
  );

#endif // UEFI_GUI_LIB_H_
