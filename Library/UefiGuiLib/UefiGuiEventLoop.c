/** @file
  Single-threaded event loop.

  Each iteration runs lv_timer_handler() in the caller's context
  (TPL_APPLICATION), then sleeps in WaitForEvent() until the next LVGL timer
  is due, bounded by Timer.MinWaitMs and Timer.MaxWaitMs, or until a
  keyboard or pointer wait event is signaled. Input is read once per
  iteration, after the wait, in the same context. No hardware is polled in
  a busy loop.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInternal.h"

/**
  Create the wait timer.
**/
EFI_STATUS
UefiGuiEventLoopInitialize (
  VOID
  )
{
  //
  // No notification function: the timer is only waited on, so nothing runs
  // at a raised TPL.
  //
  return gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &gUefiGui.WaitTimer);
}

/**
  Cancel and close the wait timer.
**/
VOID
UefiGuiEventLoopShutdown (
  VOID
  )
{
  if (gUefiGui.WaitTimer != NULL) {
    gBS->SetTimer (gUefiGui.WaitTimer, TimerCancel, 0);
    gBS->CloseEvent (gUefiGui.WaitTimer);
    gUefiGui.WaitTimer = NULL;
  }
}

/**
  Process due LVGL timers once, then sleep until the next one is due.
**/
EFI_STATUS
EFIAPI
UefiGuiProcessEvents (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      NextMs;
  UINT32      WaitMs;
  UINTN       Index;
  UINT64      Start;
  EFI_EVENT   Events[1 + UEFI_GUI_INPUT_MAX_WAIT_EVENTS];
  UINTN       InputCount;

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  if (gUefiGui.ExitRequested) {
    return EFI_ABORTED;
  }

  //
  // Read input first so LVGL handles it in this iteration.
  //
  UefiGuiLvglInputProcess ();
  NextMs = lv_timer_handler ();
  gUefiGui.Stats.LoopIterations++;
  if (gUefiGui.ExitRequested) {
    return EFI_ABORTED;
  }

  if (NextMs == LV_NO_TIMER_READY) {
    WaitMs = gUefiGui.Config.Timer.MaxWaitMs;
  } else {
    WaitMs = MAX (gUefiGui.Config.Timer.MinWaitMs, MIN (NextMs, gUefiGui.Config.Timer.MaxWaitMs));
  }

  Status = gBS->SetTimer (gUefiGui.WaitTimer, TimerRelative, MultU64x32 (WaitMs, 10000));
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("SetTimer", Status);
    return Status;
  }

  Events[0] = gUefiGui.WaitTimer;
  InputCount = 0;
  if (gUefiGui.InputInitialized) {
    InputCount = UEFI_GUI_INPUT_MAX_WAIT_EVENTS;
    if (EFI_ERROR (UefiGuiInputGetWaitEvents (&Events[1], &InputCount))) {
      InputCount = 0;
    }
  }

  Start  = UefiGuiGetTimeUs ();
  Status = gBS->WaitForEvent (1 + InputCount, Events, &Index);
  gUefiGui.Stats.WaitTimeUs += UefiGuiGetTimeUs () - Start;
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("WaitForEvent", Status);
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Run the event loop until UefiGuiRequestExit() is called.

  @retval EFI_SUCCESS   An exit was requested.
  @retval other         The loop stopped on an error (see UefiGuiGetLastError).
**/
EFI_STATUS
EFIAPI
UefiGuiRun (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  do {
    Status = UefiGuiProcessEvents ();
  } while (Status == EFI_SUCCESS);

  if (Status == EFI_ABORTED) {
    gUefiGui.ExitRequested = FALSE;
    return EFI_SUCCESS;
  }

  return Status;
}

/**
  Ask UefiGuiRun() to return after the current iteration.
**/
EFI_STATUS
EFIAPI
UefiGuiRequestExit (
  VOID
  )
{
  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  gUefiGui.ExitRequested = TRUE;
  return EFI_SUCCESS;
}
