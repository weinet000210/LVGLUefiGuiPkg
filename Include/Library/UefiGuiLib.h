/** @file
  UefiGuiLib - public API of the UefiGui framework.

  INTERFACE (Phase 0). Implemented in Phase 2 (initialize, display info,
  shutdown) and Phase 5 (event loop). These are the only UefiGui functions a
  typical application calls; it never touches GOP, input protocols or the
  LVGL display/indev setup directly.

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

#endif // UEFI_GUI_LIB_H_
