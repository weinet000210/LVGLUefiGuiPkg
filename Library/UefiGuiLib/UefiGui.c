/** @file
  UefiGuiLib public API: configuration, initialization, information and
  shutdown.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInternal.h"

UEFI_GUI_CONTEXT  gUefiGui;

/**
  Remember Status as the last error and report it.
**/
VOID
UefiGuiRecordError (
  IN CONST CHAR8  *What,
  IN EFI_STATUS   Status
  )
{
  if (!EFI_ERROR (Status)) {
    return;
  }

  gUefiGui.LastError = Status;
  if (gUefiGui.Config.Debug.ReportErrors) {
    UEFI_GUI_LOG_ERROR (What, Status);
  }
}

/**
  Fill Config with the documented defaults.
**/
VOID
EFIAPI
UefiGuiConfigInitDefaults (
  OUT UEFI_GUI_CONFIG  *Config
  )
{
  if (Config == NULL) {
    return;
  }

  ZeroMem (Config, sizeof (*Config));
  Config->Revision                     = UEFI_GUI_CONFIG_REVISION;
  Config->Display.Backend              = UefiGuiDisplayBackendAuto;
  Config->Display.DrawBufferLines      = UEFI_GUI_DEFAULT_DRAW_LINES;
  Config->Display.DoubleBuffer         = FALSE;
  Config->Input.EnableKeyboard         = TRUE;
  Config->Input.EnablePointer          = TRUE;
  Config->Input.EnableAbsolutePointer  = TRUE;
  Config->Input.ShowCursor             = TRUE;
  Config->Input.PointerSpeedPercent    = 100;
  Config->Input.EventQueueDepth        = 64;
  Config->Timer.MinWaitMs              = UEFI_GUI_DEFAULT_MIN_WAIT_MS;
  Config->Timer.MaxWaitMs              = UEFI_GUI_DEFAULT_MAX_WAIT_MS;
  Config->Memory.LimitBytes            = 0;
  Config->Debug.ReportErrors           = TRUE;
  Config->Debug.PrintStatsOnExit       = FALSE;
  Config->Debug.ShowPerformanceMonitor = FALSE;
}

/**
  Check every configuration value against its documented range.
**/
STATIC
EFI_STATUS
UefiGuiValidateConfig (
  IN CONST UEFI_GUI_CONFIG  *Config
  )
{
  if (Config->Revision != UEFI_GUI_CONFIG_REVISION) {
    return EFI_INCOMPATIBLE_VERSION;
  }

  if (((UINT32)Config->Display.Backend >= (UINT32)UefiGuiDisplayBackendMax) ||
      (Config->Display.DrawBufferLines == 0) ||
      (Config->Display.DrawBufferLines > UEFI_GUI_MAX_DRAW_LINES) ||
      ((Config->Display.PreferredWidth == 0) != (Config->Display.PreferredHeight == 0)))
  {
    return EFI_INVALID_PARAMETER;
  }

  if ((Config->Input.PointerSpeedPercent < 10) || (Config->Input.PointerSpeedPercent > 1000) ||
      (Config->Input.EventQueueDepth < 8) || (Config->Input.EventQueueDepth > 1024))
  {
    return EFI_INVALID_PARAMETER;
  }

  if ((Config->Timer.MinWaitMs == 0) || (Config->Timer.MaxWaitMs < Config->Timer.MinWaitMs) ||
      (Config->Timer.MaxWaitMs > UEFI_GUI_MAX_WAIT_LIMIT_MS))
  {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/**
  Initialize LVGL and all UefiGui backends.
**/
EFI_STATUS
EFIAPI
UefiGuiInitialize (
  IN UEFI_GUI_CONFIG  *Config  OPTIONAL
  )
{
  EFI_STATUS               Status;
  EFI_TPL                  Tpl;
  UEFI_GUI_DISPLAY_CONFIG  DisplayConfig;

  if (gUefiGui.Initialized) {
    return EFI_ALREADY_STARTED;
  }

  //
  // All GUI work happens at TPL_APPLICATION (WaitForEvent requires it).
  //
  Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  gBS->RestoreTPL (Tpl);
  if (Tpl != TPL_APPLICATION) {
    return EFI_UNSUPPORTED;
  }

  ZeroMem (&gUefiGui, sizeof (gUefiGui));
  if (Config == NULL) {
    UefiGuiConfigInitDefaults (&gUefiGui.Config);
  } else {
    Status = UefiGuiValidateConfig (Config);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    CopyMem (&gUefiGui.Config, Config, sizeof (gUefiGui.Config));
  }

  if (gUefiGui.Config.Memory.LimitBytes != 0) {
    UefiGuiSetMemoryLimit (gUefiGui.Config.Memory.LimitBytes);
  }

  Status = UefiGuiTimeInitialize ();
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("time base", Status);
    goto Fail;
  }

  ZeroMem (&DisplayConfig, sizeof (DisplayConfig));
  DisplayConfig.Backend         = gUefiGui.Config.Display.Backend;
  DisplayConfig.PreferredWidth  = gUefiGui.Config.Display.PreferredWidth;
  DisplayConfig.PreferredHeight = gUefiGui.Config.Display.PreferredHeight;
  DisplayConfig.DrawBufferLines = gUefiGui.Config.Display.DrawBufferLines;
  Status                        = UefiGuiDisplayInitialize (&DisplayConfig);
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("display", Status);
    goto Fail;
  }

  Status = UefiGuiEventLoopInitialize ();
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("event loop", Status);
    goto Fail;
  }

  Status = UefiGuiLvglInitialize ();
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("LVGL", Status);
    goto Fail;
  }

  //
  // A missing keyboard or mouse is not fatal (it is recorded as the last
  // error); only a failure to set up the queue or LVGL objects is.
  //
  Status = UefiGuiLvglInputInitialize ();
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("input", Status);
    goto Fail;
  }

  gUefiGui.Initialized = TRUE;
  return EFI_SUCCESS;

Fail:
  //
  // Release whatever was set up; Shutdown copes with partial state.
  //
  gUefiGui.Initialized = TRUE;
  UefiGuiShutdown ();
  return Status;
}

/**
  Describe the active display.
**/
EFI_STATUS
EFIAPI
UefiGuiGetDisplayInfo (
  OUT UEFI_GUI_DISPLAY_INFO  *Info
  )
{
  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  return UefiGuiDisplayGetInfo (Info);
}

/**
  Input devices in use.
**/
UINT32
EFIAPI
UefiGuiGetInputDevices (
  VOID
  )
{
  return gUefiGui.InputInitialized ? UefiGuiInputGetDevices () : 0;
}

/**
  Return the most recent error recorded by any backend.
**/
EFI_STATUS
EFIAPI
UefiGuiGetLastError (
  OUT EFI_STATUS  *Status
  )
{
  if (Status == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Status = gUefiGui.LastError;
  return EFI_SUCCESS;
}

/**
  Read the rendering statistics.
**/
EFI_STATUS
EFIAPI
UefiGuiGetRenderStats (
  OUT UEFI_GUI_RENDER_STATS  *Stats
  )
{
  if (Stats == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  CopyMem (Stats, &gUefiGui.Stats, sizeof (*Stats));
  return EFI_SUCCESS;
}

/**
  Make the next Count flushes fail before they reach the display.
**/
VOID
EFIAPI
UefiGuiDebugInjectFlushFailures (
  IN UINT32  Count
  )
{
  gUefiGui.InjectedFlushFailures = Count;
}

/**
  Stop timers, close events, release backends and deinitialize LVGL.
**/
VOID
EFIAPI
UefiGuiShutdown (
  VOID
  )
{
  UEFI_GUI_MEMORY_STATS  Memory;
  EFI_TPL                OldTpl;

  if (!gUefiGui.Initialized) {
    return;
  }

  //
  // LVGL cannot be torn down from inside one of its own callbacks.
  //
  if (gUefiGui.InLoop) {
    UefiGuiRecordError ("UefiGuiShutdown called inside the event loop", EFI_ACCESS_DENIED);
    return;
  }

  //
  // From here on, UefiGuiRequestExit() and UefiGuiPostCallback() called by
  // notification functions return EFI_NOT_STARTED.
  //
  OldTpl               = gBS->RaiseTPL (TPL_NOTIFY);
  gUefiGui.Initialized = FALSE;
  gBS->RestoreTPL (OldTpl);

  UefiGuiLvglInputShutdown ();
  UefiGuiLvglShutdown ();
  UefiGuiEventLoopShutdown ();
  UefiGuiDisplayShutdown ();
  UefiGuiEventLoopSaveStats ();

  if (gUefiGui.Config.Debug.PrintStatsOnExit) {
    UefiGuiGetMemoryStats (&Memory);
    Print (
      L"UefiGui: %lu frames, %lu flushes, %lu pixels, %lu flush errors, %lu us flushing\n",
      gUefiGui.Stats.FrameCount,
      gUefiGui.Stats.FlushCount,
      gUefiGui.Stats.FlushedPixels,
      gUefiGui.Stats.FlushErrors,
      gUefiGui.Stats.FlushTimeUs
      );
    Print (
      L"UefiGui: memory now %lu bytes, peak %lu bytes, %lu failed allocations\n",
      Memory.CurrentBytes,
      Memory.PeakBytes,
      Memory.FailureCount
      );
  }

  ZeroMem (&gUefiGui, sizeof (gUefiGui));
}
