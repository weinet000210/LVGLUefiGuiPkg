/** @file
  Single-threaded event loop.

  Each UefiGuiProcessEvents() call, in the caller's context
  (TPL_APPLICATION):
    1. runs callbacks posted with UefiGuiPostCallback(),
    2. reads queued input and hands it to the LVGL input devices,
    3. runs lv_timer_handler(),
    4. sleeps in WaitForEvent() on
         [0] the wait timer, armed for the next LVGL timer, clamped to
             Timer.MinWaitMs .. Timer.MaxWaitMs,
         [1] the exit event (UefiGuiRequestExit()),
         [2] the wake event (UefiGuiPostCallback()),
         [3..] the keyboard and pointer wait events,
         [..]  application events added with UefiGuiRegisterEvent(),
       unless work is already pending,
    5. runs the handlers of signaled application events.

  None of the events created here has a notification function, so nothing
  of UefiGuiLib runs at a raised TPL. The only functions that may be called
  at a raised TPL (up to TPL_NOTIFY) are UefiGuiRequestExit() and
  UefiGuiPostCallback(); they touch a flag, the callback ring (with the TPL
  raised to TPL_NOTIFY) and SignalEvent(), never LVGL.

  While WaitForEvent() sleeps, the DXE core halts the CPU between timer
  interrupts (idle loop), so an idle GUI costs no CPU time.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInternal.h"

#define WAIT_INDEX_TIMER  0
#define WAIT_INDEX_EXIT   1
#define WAIT_INDEX_WAKE   2
#define WAIT_FIXED_COUNT  3

//
// Statistics of the last session, kept across UefiGuiShutdown().
//
STATIC UEFI_GUI_LOOP_STATS  mLastStats;
STATIC BOOLEAN              mHaveLastStats;

/**
  Create an event without notification function and count it.
**/
STATIC
EFI_STATUS
UefiGuiCreatePlainEvent (
  IN  UINT32     Type,
  OUT EFI_EVENT  *Event
  )
{
  EFI_STATUS  Status;

  Status = gBS->CreateEvent (Type, TPL_CALLBACK, NULL, NULL, Event);
  if (!EFI_ERROR (Status)) {
    gUefiGui.LoopStats.OpenEvents++;
  }

  return Status;
}

/**
  Close an event created by UefiGuiCreatePlainEvent().
**/
STATIC
VOID
UefiGuiClosePlainEvent (
  IN OUT EFI_EVENT  *Event
  )
{
  if (*Event != NULL) {
    if (!EFI_ERROR (gBS->CloseEvent (*Event))) {
      gUefiGui.LoopStats.OpenEvents--;
    }

    *Event = NULL;
  }
}

/**
  Create the wait timer, the exit event and the wake event.
**/
EFI_STATUS
UefiGuiEventLoopInitialize (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = UefiGuiCreatePlainEvent (EVT_TIMER, &gUefiGui.WaitTimer);
  if (!EFI_ERROR (Status)) {
    Status = UefiGuiCreatePlainEvent (0, &gUefiGui.ExitEvent);
  }

  if (!EFI_ERROR (Status)) {
    Status = UefiGuiCreatePlainEvent (0, &gUefiGui.WakeEvent);
  }

  return Status;
}

/**
  Cancel the wait timer, close the loop's events, drop posted callbacks and
  forget application events (which belong to the application and stay open).
**/
VOID
UefiGuiEventLoopShutdown (
  VOID
  )
{
  EFI_TPL  OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  gUefiGui.LoopStats.CallbacksDropped += gUefiGui.PostedCount;
  gUefiGui.PostedCount                 = 0;
  gUefiGui.PostedHead                  = 0;
  gBS->RestoreTPL (OldTpl);

  if (gUefiGui.WaitTimer != NULL) {
    gBS->SetTimer (gUefiGui.WaitTimer, TimerCancel, 0);
  }

  UefiGuiClosePlainEvent (&gUefiGui.WaitTimer);
  UefiGuiClosePlainEvent (&gUefiGui.ExitEvent);
  UefiGuiClosePlainEvent (&gUefiGui.WakeEvent);

  ZeroMem (gUefiGui.AppEvents, sizeof (gUefiGui.AppEvents));
  gUefiGui.AppEventCount = 0;
}

/**
  Take the oldest posted callback.

  @retval TRUE   Entry filled.
  @retval FALSE  The queue is empty.
**/
STATIC
BOOLEAN
UefiGuiTakePosted (
  OUT UEFI_GUI_POSTED_CALLBACK  *Entry
  )
{
  EFI_TPL  OldTpl;
  BOOLEAN  Found;

  Found  = FALSE;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (gUefiGui.PostedCount > 0) {
    *Entry              = gUefiGui.Posted[gUefiGui.PostedHead];
    gUefiGui.PostedHead = (gUefiGui.PostedHead + 1) % UEFI_GUI_MAX_POSTED_CALLBACKS;
    gUefiGui.PostedCount--;
    Found = TRUE;
  }

  gBS->RestoreTPL (OldTpl);
  return Found;
}

/**
  Run the callbacks that were queued when this function started. Callbacks
  posted by these callbacks run in the next iteration, so a callback that
  re-posts itself cannot starve the loop.
**/
STATIC
VOID
UefiGuiRunPosted (
  VOID
  )
{
  UEFI_GUI_POSTED_CALLBACK  Entry;
  UINT32                    Budget;

  Budget = gUefiGui.PostedCount;
  while ((Budget > 0) && UefiGuiTakePosted (&Entry)) {
    Budget--;
    gUefiGui.LoopStats.CallbacksRun++;
    Entry.Callback (Entry.Context);
    if (gUefiGui.ExitRequested) {
      break;
    }
  }
}

/**
  Run the handlers of application events marked pending.
**/
STATIC
VOID
UefiGuiRunAppEvents (
  VOID
  )
{
  UINTN               Index;
  UEFI_GUI_APP_EVENT  *App;

  for (Index = 0; Index < UEFI_GUI_MAX_APP_EVENTS; Index++) {
    App = &gUefiGui.AppEvents[Index];
    if ((App->Event != NULL) && App->Pending) {
      App->Pending = FALSE;
      gUefiGui.LoopStats.AppEventsHandled++;
      App->Handler (App->Event, App->Context);
    }
  }
}

/**
  TRUE when the next iteration has work to do without waiting.
**/
STATIC
BOOLEAN
UefiGuiWorkPending (
  VOID
  )
{
  UINTN  Index;

  if (gUefiGui.ExitRequested || (gUefiGui.PostedCount > 0)) {
    return TRUE;
  }

  for (Index = 0; Index < UEFI_GUI_MAX_APP_EVENTS; Index++) {
    if ((gUefiGui.AppEvents[Index].Event != NULL) && gUefiGui.AppEvents[Index].Pending) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Sleep until the timer expires or any other wait event is signaled, then
  record why the loop woke and consume stale signals.
**/
STATIC
EFI_STATUS
UefiGuiWait (
  IN UINT32  WaitMs
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Events[WAIT_FIXED_COUNT + UEFI_GUI_INPUT_MAX_WAIT_EVENTS + UEFI_GUI_MAX_APP_EVENTS];
  UINT8       AppSlot[WAIT_FIXED_COUNT + UEFI_GUI_INPUT_MAX_WAIT_EVENTS + UEFI_GUI_MAX_APP_EVENTS];
  UINTN       Count;
  UINTN       InputCount;
  UINTN       InputFirst;
  UINTN       AppFirst;
  UINTN       Index;
  UINTN       Slot;
  UINT64      Start;

  //
  // A timer that expired after the previous wake would end this wait at
  // once: consume its signal before arming it again.
  //
  gBS->CheckEvent (gUefiGui.WaitTimer);
  Status = gBS->SetTimer (gUefiGui.WaitTimer, TimerRelative, MultU64x32 (WaitMs, 10000));
  if (EFI_ERROR (Status)) {
    UefiGuiRecordError ("SetTimer", Status);
    return Status;
  }

  Events[WAIT_INDEX_TIMER] = gUefiGui.WaitTimer;
  Events[WAIT_INDEX_EXIT]  = gUefiGui.ExitEvent;
  Events[WAIT_INDEX_WAKE]  = gUefiGui.WakeEvent;
  Count                    = WAIT_FIXED_COUNT;

  InputFirst = Count;
  InputCount = 0;
  if (gUefiGui.InputInitialized) {
    InputCount = UEFI_GUI_INPUT_MAX_WAIT_EVENTS;
    if (EFI_ERROR (UefiGuiInputGetWaitEvents (&Events[Count], &InputCount))) {
      InputCount = 0;
    }
  }

  Count   += InputCount;
  AppFirst = Count;
  for (Slot = 0; Slot < UEFI_GUI_MAX_APP_EVENTS; Slot++) {
    if (gUefiGui.AppEvents[Slot].Event != NULL) {
      AppSlot[Count] = (UINT8)Slot;
      Events[Count]  = gUefiGui.AppEvents[Slot].Event;
      Count++;
    }
  }

  Start  = UefiGuiGetTimeUs ();
  Status = gBS->WaitForEvent (Count, Events, &Index);
  gUefiGui.LoopStats.WaitTimeUs += UefiGuiGetTimeUs () - Start;
  gUefiGui.Stats.WaitTimeUs      = gUefiGui.LoopStats.WaitTimeUs;
  if (EFI_ERROR (Status)) {
    gBS->SetTimer (gUefiGui.WaitTimer, TimerCancel, 0);
    UefiGuiRecordError ("WaitForEvent", Status);
    return Status;
  }

  if (Index == WAIT_INDEX_TIMER) {
    gUefiGui.LoopStats.TimerWakes++;
  } else {
    //
    // Woken early: disarm the timer and drop a signal that raced in.
    //
    gBS->SetTimer (gUefiGui.WaitTimer, TimerCancel, 0);
    gBS->CheckEvent (gUefiGui.WaitTimer);
    if (Index == WAIT_INDEX_EXIT) {
      gUefiGui.LoopStats.ExitWakes++;
    } else if (Index == WAIT_INDEX_WAKE) {
      gUefiGui.LoopStats.CallbackWakes++;
    } else if ((Index >= InputFirst) && (Index < InputFirst + InputCount)) {
      gUefiGui.LoopStats.InputWakes++;
    } else if ((Index >= AppFirst) && (Index < Count)) {
      gUefiGui.LoopStats.AppEventWakes++;
      gUefiGui.AppEvents[AppSlot[Index]].Pending = TRUE;
    }
  }

  //
  // WaitForEvent() consumed only the signal of the event it returned.
  // Collect the other application events that are signaled too, and clear
  // the exit and wake events: the flag and the ring hold their state, and
  // a request made after this point signals again.
  //
  for (Index = AppFirst; Index < Count; Index++) {
    Slot = AppSlot[Index];
    if (!gUefiGui.AppEvents[Slot].Pending && (gBS->CheckEvent (Events[Index]) == EFI_SUCCESS)) {
      gUefiGui.AppEvents[Slot].Pending = TRUE;
    }
  }

  gBS->CheckEvent (gUefiGui.ExitEvent);
  gBS->CheckEvent (gUefiGui.WakeEvent);
  return EFI_SUCCESS;
}

/**
  One loop iteration (see the file header).
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
  UINT64      Start;
  UINT64      Busy;

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  if (gUefiGui.InLoop) {
    return EFI_ACCESS_DENIED;
  }

  if (gUefiGui.ExitRequested) {
    return EFI_ABORTED;
  }

  gUefiGui.InLoop = TRUE;
  Start           = UefiGuiGetTimeUs ();
  Status          = EFI_SUCCESS;
  gUefiGui.LoopStats.Iterations++;
  gUefiGui.Stats.LoopIterations = gUefiGui.LoopStats.Iterations;

  UefiGuiRunPosted ();
  UefiGuiLvglInputProcess ();
  NextMs = lv_timer_handler ();

  if (gUefiGui.ExitRequested) {
    Status = EFI_ABORTED;
  } else if (UefiGuiWorkPending ()) {
    gUefiGui.LoopStats.SkippedWaits++;
  } else {
    if (NextMs == LV_NO_TIMER_READY) {
      WaitMs = gUefiGui.Config.Timer.MaxWaitMs;
    } else {
      WaitMs = MAX (gUefiGui.Config.Timer.MinWaitMs, MIN (NextMs, gUefiGui.Config.Timer.MaxWaitMs));
    }

    Busy = UefiGuiGetTimeUs () - Start;
    gUefiGui.LoopStats.BusyTimeUs += Busy;
    if (Busy > gUefiGui.LoopStats.LongestIterationUs) {
      gUefiGui.LoopStats.LongestIterationUs = Busy;
    }

    Status = UefiGuiWait (WaitMs);
    Start  = UefiGuiGetTimeUs ();
    if (!EFI_ERROR (Status)) {
      UefiGuiRunAppEvents ();
      if (gUefiGui.ExitRequested) {
        Status = EFI_ABORTED;
      }
    }
  }

  Busy = UefiGuiGetTimeUs () - Start;
  gUefiGui.LoopStats.BusyTimeUs += Busy;
  if (Busy > gUefiGui.LoopStats.LongestIterationUs) {
    gUefiGui.LoopStats.LongestIterationUs = Busy;
  }

  gUefiGui.InLoop = FALSE;
  return Status;
}

/**
  Run the event loop until UefiGuiRequestExit() is called.
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

  if (gUefiGui.InLoop) {
    return EFI_ACCESS_DENIED;
  }

  do {
    Status = UefiGuiProcessEvents ();
  } while (Status == EFI_SUCCESS);

  if (Status == EFI_ABORTED) {
    //
    // Consume the request: clear the flag and the exit event's signal, or
    // the next UefiGuiRun() would wake once for nothing.
    //
    gUefiGui.ExitRequested = FALSE;
    gBS->CheckEvent (gUefiGui.ExitEvent);
    return EFI_SUCCESS;
  }

  return Status;
}

/**
  Ask UefiGuiRun() to return. Callable at up to TPL_NOTIFY.
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
  if (gUefiGui.ExitEvent != NULL) {
    gBS->SignalEvent (gUefiGui.ExitEvent);
  }

  return EFI_SUCCESS;
}

/**
  Queue Callback for the GUI context. Callable at up to TPL_NOTIFY.
**/
EFI_STATUS
EFIAPI
UefiGuiPostCallback (
  IN UEFI_GUI_CALLBACK  Callback,
  IN VOID               *Context  OPTIONAL
  )
{
  EFI_TPL     OldTpl;
  EFI_STATUS  Status;
  UINT32      Tail;

  if (Callback == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (gUefiGui.PostedCount >= UEFI_GUI_MAX_POSTED_CALLBACKS) {
    gUefiGui.LoopStats.CallbacksRejected++;
    Status = EFI_OUT_OF_RESOURCES;
  } else {
    Tail                             = (gUefiGui.PostedHead + gUefiGui.PostedCount) % UEFI_GUI_MAX_POSTED_CALLBACKS;
    gUefiGui.Posted[Tail].Callback = Callback;
    gUefiGui.Posted[Tail].Context  = Context;
    gUefiGui.PostedCount++;
    Status = EFI_SUCCESS;
  }

  gBS->RestoreTPL (OldTpl);

  if (!EFI_ERROR (Status) && (gUefiGui.WakeEvent != NULL)) {
    gBS->SignalEvent (gUefiGui.WakeEvent);
  }

  return Status;
}

/**
  Add an application event to the wait set.
**/
EFI_STATUS
EFIAPI
UefiGuiRegisterEvent (
  IN EFI_EVENT               Event,
  IN UEFI_GUI_EVENT_HANDLER  Handler,
  IN VOID                    *Context  OPTIONAL
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  UINTN       Free;

  if ((Event == NULL) || (Handler == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  Free = UEFI_GUI_MAX_APP_EVENTS;
  for (Index = 0; Index < UEFI_GUI_MAX_APP_EVENTS; Index++) {
    if (gUefiGui.AppEvents[Index].Event == Event) {
      return EFI_ALREADY_STARTED;
    }

    if ((gUefiGui.AppEvents[Index].Event == NULL) && (Free == UEFI_GUI_MAX_APP_EVENTS)) {
      Free = Index;
    }
  }

  if (Free == UEFI_GUI_MAX_APP_EVENTS) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // CheckEvent() rejects EVT_NOTIFY_SIGNAL events exactly like
  // WaitForEvent() would. A signal already present is kept as pending.
  //
  Status = gBS->CheckEvent (Event);
  if ((Status != EFI_SUCCESS) && (Status != EFI_NOT_READY)) {
    return EFI_INVALID_PARAMETER;
  }

  gUefiGui.AppEvents[Free].Event   = Event;
  gUefiGui.AppEvents[Free].Handler = Handler;
  gUefiGui.AppEvents[Free].Context = Context;
  gUefiGui.AppEvents[Free].Pending = (BOOLEAN)(Status == EFI_SUCCESS);
  gUefiGui.AppEventCount++;
  return EFI_SUCCESS;
}

/**
  Remove an application event from the wait set.
**/
EFI_STATUS
EFIAPI
UefiGuiUnregisterEvent (
  IN EFI_EVENT  Event
  )
{
  UINTN  Index;

  if (!gUefiGui.Initialized) {
    return EFI_NOT_STARTED;
  }

  for (Index = 0; Index < UEFI_GUI_MAX_APP_EVENTS; Index++) {
    if ((Event != NULL) && (gUefiGui.AppEvents[Index].Event == Event)) {
      ZeroMem (&gUefiGui.AppEvents[Index], sizeof (gUefiGui.AppEvents[Index]));
      gUefiGui.AppEventCount--;
      return EFI_SUCCESS;
    }
  }

  return EFI_NOT_FOUND;
}

/**
  Read the event loop statistics.
**/
EFI_STATUS
EFIAPI
UefiGuiGetLoopStats (
  OUT UEFI_GUI_LOOP_STATS  *Stats
  )
{
  if (Stats == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGui.Initialized) {
    if (!mHaveLastStats) {
      return EFI_NOT_STARTED;
    }

    CopyMem (Stats, &mLastStats, sizeof (*Stats));
    return EFI_SUCCESS;
  }

  CopyMem (Stats, &gUefiGui.LoopStats, sizeof (*Stats));
  Stats->RegisteredEvents = gUefiGui.AppEventCount;
  return EFI_SUCCESS;
}

/**
  Keep the statistics of the session that is ending.
**/
VOID
UefiGuiEventLoopSaveStats (
  VOID
  )
{
  CopyMem (&mLastStats, &gUefiGui.LoopStats, sizeof (mLastStats));
  mLastStats.RegisteredEvents = gUefiGui.AppEventCount;
  mHaveLastStats              = TRUE;
}
