/** @file
  RuntimeTest - Phase 5 tests: event loop, timers, wake sources, exit and
  cleanup, on the real firmware (QEMU + OVMF).

  Sections (each one a separate UefiGuiInitialize() / UefiGuiShutdown()
  session, which also exercises re-initialization):
    1. Idle and timers (default configuration): an idle screen sleeps (loop
       wake count, time in WaitForEvent, host CPU time measured by the QEMU
       driver against a deliberate busy loop), LVGL timer period accuracy,
       animation duration.
    2. Wake sources with Timer.MaxWaitMs = 1000: callbacks posted from a
       TPL_CALLBACK notification function (latency, TPL, ordering, queue
       limit, self re-posting), application events (signal, periodic timer,
       validation, limits, unregister), exit requests from an LVGL timer,
       from a notification function and from the keyboard, re-entry guards,
       keyboard wake latency.
    3. Timers and input together: a 20 ms LVGL timer and an endless
       animation keep running while the host types into a text area.
    4. Cleanup: memory back to its starting value after every session, no
       loop event left open, queued callbacks discarded, registered events
       left open for their owner, GOP mode restored after a mode change,
       five init / shutdown cycles without memory growth.

  Input comes from the host: "UEFIGUI-CMD <seq> <cmd>" lines on the serial
  port are executed by scripts/qemu-driver.py ("sendkey", plus "cpu begin"
  and "cpu end <label>" to measure the QEMU process CPU time).

  The last console line is "UEFIGUI-RUNTIMETEST: PASS" or "... FAIL".

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/LvglLib.h>
#include <Library/UefiGuiLib.h>
#include <Library/UefiGuiRuntimeLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SerialIo.h>
#include <Protocol/Timer.h>

#define MAX_RESULTS      200
#define STEP_TIMEOUT_MS  8000

typedef struct {
  CONST CHAR16    *Name;
  BOOLEAN         Passed;
  BOOLEAN         HasValue;
  INT64           Value;
} RESULT;

STATIC RESULT                  mResults[MAX_RESULTS];
STATIC UINTN                   mResultCount;
STATIC EFI_SERIAL_IO_PROTOCOL  *mSerial;
STATIC UINTN                   mSeq;

STATIC
VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed
  )
{
  if (mResultCount < MAX_RESULTS) {
    mResults[mResultCount].Name     = Name;
    mResults[mResultCount].Passed   = Passed;
    mResults[mResultCount].HasValue = FALSE;
    mResultCount++;
  }
}

STATIC
VOID
CheckValue (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed,
  IN INT64         Value
  )
{
  Check (Name, Passed);
  if (mResultCount > 0) {
    mResults[mResultCount - 1].HasValue = TRUE;
    mResults[mResultCount - 1].Value    = Value;
  }
}

/**
  Write one host command line to the serial port.
**/
STATIC
VOID
SendCmd (
  IN CONST CHAR8  *Commands
  )
{
  CHAR8  Line[512];
  UINTN  Size;

  mSeq++;
  AsciiSPrint (Line, sizeof (Line), "\r\nUEFIGUI-CMD %u %a\r\n", (UINT32)mSeq, Commands);
  Size = AsciiStrLen (Line);
  if (mSerial != NULL) {
    mSerial->Write (mSerial, &Size, Line);
  }
}

/**
  Run the loop for Ms milliseconds.
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
    if (UefiGuiProcessEvents () != EFI_SUCCESS) {
      break;
    }
  }
}

/**
  Current TPL, read without changing it.
**/
STATIC
EFI_TPL
CurrentTpl (
  VOID
  )
{
  EFI_TPL  Tpl;

  Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  gBS->RestoreTPL (Tpl);
  return Tpl;
}

STATIC
EFI_STATUS
InitWith (
  IN UINT32  MaxWaitMs,
  IN UINT32  Width,
  IN UINT32  Height
  )
{
  UEFI_GUI_CONFIG  Config;

  UefiGuiConfigInitDefaults (&Config);
  if (MaxWaitMs != 0) {
    Config.Timer.MaxWaitMs = MaxWaitMs;
  }

  Config.Display.PreferredWidth  = Width;
  Config.Display.PreferredHeight = Height;
  return UefiGuiInitialize (&Config);
}

/**
  Memory currently allocated through UefiGuiAllocate().
**/
STATIC
UINT64
GuiBytes (
  VOID
  )
{
  UEFI_GUI_MEMORY_STATS  Stats;

  UefiGuiGetMemoryStats (&Stats);
  return Stats.CurrentBytes;
}

/**
  Shut down and check that every resource of the session is gone.
**/
STATIC
VOID
ShutdownAndCheck (
  IN CONST CHAR16  *MemName,
  IN CONST CHAR16  *EventsName,
  IN UINT64        BaselineBytes
  )
{
  UEFI_GUI_LOOP_STATS  Loop;

  UefiGuiShutdown ();
  CheckValue (MemName, (BOOLEAN)(GuiBytes () == BaselineBytes), (INT64)(GuiBytes () - BaselineBytes));
  ZeroMem (&Loop, sizeof (Loop));
  UefiGuiGetLoopStats (&Loop);
  CheckValue (EventsName, (BOOLEAN)(Loop.OpenEvents == 0), Loop.OpenEvents);
}

// ---------------------------------------------------------------------------
// Section 1: idle and timers.
// ---------------------------------------------------------------------------

STATIC UINT32  mTimerCalls;
STATIC UINT64  mTimerLast;
STATIC UINT64  mTimerMaxGap;
STATIC UINT64  mTimerMinGap;
STATIC UINT64  mAnimEnd;

STATIC
void
PeriodicCb (
  lv_timer_t  *Timer
  )
{
  UINT64  Now;
  UINT64  Gap;

  Now = UefiGuiGetTimeMs ();
  if (mTimerCalls > 0) {
    Gap          = Now - mTimerLast;
    mTimerMaxGap = MAX (mTimerMaxGap, Gap);
    mTimerMinGap = MIN (mTimerMinGap, Gap);
  }

  mTimerLast = Now;
  mTimerCalls++;
}

STATIC
void
AnimX (
  void     *Obj,
  int32_t  Value
  )
{
  lv_obj_set_x ((lv_obj_t *)Obj, Value);
}

STATIC
void
AnimDone (
  lv_anim_t  *Anim
  )
{
  mAnimEnd = UefiGuiGetTimeMs ();
}

STATIC
VOID
SectionIdleAndTimers (
  IN UINT64  Baseline
  )
{
  lv_obj_t               *Label;
  lv_obj_t               *Box;
  lv_timer_t             *Timer;
  lv_anim_t              Anim;
  UEFI_GUI_LOOP_STATS    L0;
  UEFI_GUI_LOOP_STATS    L1;
  UEFI_GUI_RENDER_STATS  R0;
  UEFI_GUI_RENDER_STATS  R1;
  UEFI_GUI_RENDER_STATS  R2;
  UINT64                 Start;
  UINT64                 Elapsed;
  UINT64                 Waited;
  UINT64                 End;
  UINT64                 AnimStart;
  UINT64                 Spins;
  EFI_STATUS             Status;

  Status = InitWith (0, 0, 0);
  Check (L"1 init (defaults, MaxWaitMs 100)", (BOOLEAN)!EFI_ERROR (Status));
  if (EFI_ERROR (Status)) {
    return;
  }

  Label = lv_label_create (lv_screen_active ());
  lv_label_set_text (Label, "RuntimeTest: idle, timers, animation");
  lv_obj_align (Label, LV_ALIGN_TOP_MID, 0, 20);
  Box = lv_obj_create (lv_screen_active ());
  lv_obj_set_size (Box, 80, 80);
  lv_obj_set_pos (Box, 40, 200);
  Pump (500);

  //
  // Idle: nothing invalidated, no animation, no LVGL timer of ours.
  //
  UefiGuiGetLoopStats (&L0);
  UefiGuiGetRenderStats (&R0);
  SendCmd ("cpu begin");
  Start = UefiGuiGetTimeUs ();
  Pump (3000);
  Elapsed = UefiGuiGetTimeUs () - Start;
  SendCmd ("cpu end idle");
  UefiGuiGetLoopStats (&L1);
  UefiGuiGetRenderStats (&R1);
  Waited = L1.WaitTimeUs - L0.WaitTimeUs;
  CheckValue (L"1 idle 3 s: loop iterations <= 40", (BOOLEAN)(L1.Iterations - L0.Iterations <= 40), (INT64)(L1.Iterations - L0.Iterations));
  CheckValue (L"1 idle 3 s: timer wakes >= 25 (sleeps MaxWaitMs)", (BOOLEAN)(L1.TimerWakes - L0.TimerWakes >= 25), (INT64)(L1.TimerWakes - L0.TimerWakes));
  CheckValue (L"1 idle 3 s: per mille of time in WaitForEvent >= 980", (BOOLEAN)(Waited * 1000 >= Elapsed * 980), (INT64)DivU64x64Remainder (Waited * 1000, Elapsed, NULL));
  CheckValue (L"1 idle 3 s: frames rendered == 0", (BOOLEAN)(R1.FrameCount == R0.FrameCount), (INT64)(R1.FrameCount - R0.FrameCount));
  CheckValue (L"1 idle 3 s: skipped waits == 0", (BOOLEAN)(L1.SkippedWaits == L0.SkippedWaits), (INT64)(L1.SkippedWaits - L0.SkippedWaits));

  //
  // Reference for the host CPU measurement: what busy polling costs.
  //
  SendCmd ("cpu begin");
  End   = UefiGuiGetTimeMs () + 3000;
  Spins = 0;
  while (UefiGuiGetTimeMs () < End) {
    gBS->CheckEvent (gST->ConIn->WaitForKey);
    Spins++;
  }

  SendCmd ("cpu end busy");
  CheckValue (L"1 busy reference loop ran (CheckEvent calls)", (BOOLEAN)(Spins > 0), (INT64)Spins);
  Pump (300);

  //
  // LVGL timer, 100 ms period, for 2 s.
  //
  mTimerCalls  = 0;
  mTimerMaxGap = 0;
  mTimerMinGap = MAX_UINT64;
  Timer        = lv_timer_create (PeriodicCb, 100, NULL);
  Start        = UefiGuiGetTimeMs ();
  Pump (2000);
  lv_timer_delete (Timer);
  CheckValue (L"1 LVGL timer 100 ms over 2 s: calls 19..21", (BOOLEAN)((mTimerCalls >= 19) && (mTimerCalls <= 21)), mTimerCalls);
  CheckValue (L"1 LVGL timer: shortest gap >= 95 ms", (BOOLEAN)(mTimerMinGap >= 95), (INT64)mTimerMinGap);
  CheckValue (L"1 LVGL timer: longest gap <= 125 ms", (BOOLEAN)(mTimerMaxGap <= 125), (INT64)mTimerMaxGap);

  //
  // 600 ms animation.
  //
  UefiGuiGetRenderStats (&R1);
  mAnimEnd = 0;
  lv_anim_init (&Anim);
  lv_anim_set_var (&Anim, Box);
  lv_anim_set_exec_cb (&Anim, AnimX);
  lv_anim_set_values (&Anim, 40, 840);
  lv_anim_set_duration (&Anim, 600);
  lv_anim_set_completed_cb (&Anim, AnimDone);
  AnimStart = UefiGuiGetTimeMs ();
  lv_anim_start (&Anim);
  End = AnimStart + 2000;
  while ((mAnimEnd == 0) && (UefiGuiGetTimeMs () < End)) {
    UefiGuiProcessEvents ();
  }

  Pump (100);
  UefiGuiGetRenderStats (&R2);
  CheckValue (L"1 animation 600 ms: completed in 600..680 ms", (BOOLEAN)((mAnimEnd != 0) && (mAnimEnd - AnimStart >= 600) && (mAnimEnd - AnimStart <= 680)), (INT64)(mAnimEnd - AnimStart));
  CheckValue (L"1 animation: frames rendered >= 20", (BOOLEAN)(R2.FrameCount - R1.FrameCount >= 20), (INT64)(R2.FrameCount - R1.FrameCount));
  CheckValue (L"1 animation: box ended at x = 840", (BOOLEAN)(lv_obj_get_x (Box) == 840), lv_obj_get_x (Box));

  ShutdownAndCheck (L"1 shutdown: UefiGui memory back to start", L"1 shutdown: loop events open", Baseline);
}

// ---------------------------------------------------------------------------
// Section 2: wake sources, exit, re-entry (MaxWaitMs 1000).
// ---------------------------------------------------------------------------

STATIC UINT32   mPostedRun;
STATIC UINT64   mPostedMaxLatencyUs;
STATIC UINT64   mPostedSumLatencyUs;
STATIC BOOLEAN  mPostedTplOk;
STATIC UINT32   mOrderNext;
STATIC BOOLEAN  mOrderOk;
STATIC UINT32   mRepost;
STATIC UINT32   mAppCalls[4];
STATIC BOOLEAN  mAppTplOk;
STATIC UINT64   mExitAtUs;
STATIC UINT64   mKeyAtUs;
STATIC UINT32   mKeysSeen;
STATIC BOOLEAN  mEscExit;
STATIC UINT32   mReentry;

STATIC
VOID
EFIAPI
PostedCb (
  IN VOID  *Context
  )
{
  UINT64  Latency;

  Latency             = UefiGuiGetTimeUs () - *(UINT64 *)Context;
  mPostedMaxLatencyUs = MAX (mPostedMaxLatencyUs, Latency);
  mPostedSumLatencyUs += Latency;
  mPostedTplOk        = (BOOLEAN)(mPostedTplOk && (CurrentTpl () == TPL_APPLICATION));
  mPostedRun++;
}

STATIC UINT64  mPostStamp[64];
STATIC UINT32  mPostCount;

/**
  Timer notification at TPL_CALLBACK: must not touch LVGL, so it posts.
**/
STATIC
VOID
EFIAPI
PostFromNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mPostCount < ARRAY_SIZE (mPostStamp)) {
    mPostStamp[mPostCount] = UefiGuiGetTimeUs ();
    UefiGuiPostCallback (PostedCb, &mPostStamp[mPostCount]);
    mPostCount++;
  }
}

STATIC
VOID
EFIAPI
OrderCb (
  IN VOID  *Context
  )
{
  if ((UINT32)(UINTN)Context != mOrderNext) {
    mOrderOk = FALSE;
  }

  mOrderNext++;
}

STATIC
VOID
EFIAPI
RepostCb (
  IN VOID  *Context
  )
{
  mRepost++;
  if (mRepost < 100) {
    UefiGuiPostCallback (RepostCb, NULL);
  }
}

STATIC
VOID
EFIAPI
AppHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mAppCalls[(UINTN)Context]++;
  mAppTplOk = (BOOLEAN)(mAppTplOk && (CurrentTpl () == TPL_APPLICATION));
}

STATIC
VOID
EFIAPI
AppSelfRemove (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mAppCalls[3]++;
  UefiGuiUnregisterEvent (Event);
}

STATIC
VOID
EFIAPI
DummyNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
}

STATIC
void
ExitFromTimer (
  lv_timer_t  *Timer
  )
{
  lv_timer_delete (Timer);
  UefiGuiRequestExit ();
}

STATIC
VOID
EFIAPI
ExitFromNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mExitAtUs = UefiGuiGetTimeUs ();
  UefiGuiRequestExit ();
}

STATIC
void
ReentryCb (
  lv_timer_t  *Timer
  )
{
  lv_timer_delete (Timer);
  if (UefiGuiProcessEvents () == EFI_ACCESS_DENIED) {
    mReentry |= 1;
  }

  if (UefiGuiRun () == EFI_ACCESS_DENIED) {
    mReentry |= 2;
  }

  //
  // Must be refused here (LVGL is running this callback).
  //
  UefiGuiShutdown ();
}

STATIC
BOOLEAN
EFIAPI
KeyHandler (
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN VOID                      *Context
  )
{
  if (Key->ScanCode == SCAN_ESC) {
    mEscExit = TRUE;
    UefiGuiRequestExit ();
    return TRUE;
  }

  if (Key->UnicodeChar == L'a') {
    mKeyAtUs = UefiGuiGetTimeUs ();
    mKeysSeen++;
    return TRUE;
  }

  return FALSE;
}

STATIC
VOID
SectionWakeSources (
  IN UINT64  Baseline
  )
{
  EFI_STATUS           Status;
  EFI_EVENT            Notify;
  EFI_EVENT            Plain;
  EFI_EVENT            Periodic;
  EFI_EVENT            SelfRemove;
  EFI_EVENT            Signal;
  EFI_EVENT            Extra[8];
  EFI_TPL              OldTpl;
  UEFI_GUI_LOOP_STATS  L0;
  UEFI_GUI_LOOP_STATS  L1;
  UINT32               Index;
  UINT32               Accepted;
  UINT64               Start;
  UINT64               End;
  UINT64               It0;
  UINT64               SendAt;
  UINT64               MaxKeyMs;
  UINT64               SumKeyMs;
  UINT32               Keys;
  BOOLEAN              Ok;

  Status = InitWith (1000, 0, 0);
  Check (L"2 init (MaxWaitMs 1000)", (BOOLEAN)!EFI_ERROR (Status));
  if (EFI_ERROR (Status)) {
    return;
  }

  lv_label_set_text (lv_label_create (lv_screen_active ()), "RuntimeTest: wake sources");
  Pump (300);

  //
  // Callbacks posted by a TPL_CALLBACK notification function every 150 ms.
  //
  mPostedRun          = 0;
  mPostCount          = 0;
  mPostedMaxLatencyUs = 0;
  mPostedSumLatencyUs = 0;
  mPostedTplOk        = TRUE;
  Status              = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, PostFromNotify, NULL, &Notify);
  Check (L"2 create TPL_CALLBACK notify timer", (BOOLEAN)!EFI_ERROR (Status));
  UefiGuiGetLoopStats (&L0);
  gBS->SetTimer (Notify, TimerPeriodic, 150 * 10000);
  Pump (1600);
  gBS->SetTimer (Notify, TimerCancel, 0);
  gBS->CloseEvent (Notify);
  Pump (50);
  UefiGuiGetLoopStats (&L1);
  CheckValue (L"2 posted from notify: callbacks run", (BOOLEAN)((mPostedRun >= 9) && (mPostedRun == mPostCount)), mPostedRun);
  CheckValue (L"2 posted from notify: callbacks ran at TPL_APPLICATION", mPostedTplOk, mPostedRun);
  CheckValue (L"2 posted from notify: max latency us < 5000", (BOOLEAN)(mPostedMaxLatencyUs < 5000), (INT64)mPostedMaxLatencyUs);
  CheckValue (
    L"2 posted from notify: mean latency us",
    (BOOLEAN)(mPostedRun > 0),
    (mPostedRun > 0) ? (INT64)DivU64x32 (mPostedSumLatencyUs, mPostedRun) : 0
    );
  CheckValue (L"2 posted from notify: callback wakes >= 9", (BOOLEAN)(L1.CallbackWakes - L0.CallbackWakes >= 9), (INT64)(L1.CallbackWakes - L0.CallbackWakes));
  CheckValue (L"2 posted from notify: timer wakes <= 4 (MaxWaitMs 1000)", (BOOLEAN)(L1.TimerWakes - L0.TimerWakes <= 4), (INT64)(L1.TimerWakes - L0.TimerWakes));

  //
  // Ordering and the queue limit, posted at TPL_NOTIFY.
  //
  mOrderNext = 0;
  mOrderOk   = TRUE;
  Accepted   = 0;
  UefiGuiGetLoopStats (&L0);
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (Index = 0; Index < UEFI_GUI_MAX_POSTED_CALLBACKS + 1; Index++) {
    Status = UefiGuiPostCallback (OrderCb, (VOID *)(UINTN)Index);
    if (!EFI_ERROR (Status)) {
      Accepted++;
    }
  }

  gBS->RestoreTPL (OldTpl);
  CheckValue (L"2 queue limit: accepted == 32", (BOOLEAN)(Accepted == UEFI_GUI_MAX_POSTED_CALLBACKS), Accepted);
  Check (L"2 queue limit: 33rd post -> EFI_OUT_OF_RESOURCES", (BOOLEAN)(Status == EFI_OUT_OF_RESOURCES));
  Pump (100);
  UefiGuiGetLoopStats (&L1);
  CheckValue (L"2 queue: all 32 ran in posting order", (BOOLEAN)(mOrderOk && (mOrderNext == UEFI_GUI_MAX_POSTED_CALLBACKS)), mOrderNext);
  CheckValue (L"2 queue: rejected counted", (BOOLEAN)(L1.CallbacksRejected - L0.CallbacksRejected == 1), (INT64)(L1.CallbacksRejected - L0.CallbacksRejected));
  Check (L"2 PostCallback(NULL) -> EFI_INVALID_PARAMETER", (BOOLEAN)(UefiGuiPostCallback (NULL, NULL) == EFI_INVALID_PARAMETER));

  //
  // A callback that re-posts itself runs once per iteration.
  //
  mRepost = 0;
  UefiGuiGetLoopStats (&L0);
  UefiGuiPostCallback (RepostCb, NULL);
  End = UefiGuiGetTimeMs () + 3000;
  while ((mRepost < 100) && (UefiGuiGetTimeMs () < End)) {
    UefiGuiProcessEvents ();
  }

  UefiGuiGetLoopStats (&L1);
  CheckValue (L"2 re-posting callback: 100 runs", (BOOLEAN)(mRepost == 100), mRepost);
  CheckValue (L"2 re-posting callback: one run per iteration", (BOOLEAN)(L1.Iterations - L0.Iterations >= 100), (INT64)(L1.Iterations - L0.Iterations));

  //
  // Application events.
  //
  ZeroMem (mAppCalls, sizeof (mAppCalls));
  mAppTplOk = TRUE;
  gBS->CreateEvent (0, TPL_CALLBACK, NULL, NULL, &Plain);
  gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Periodic);
  gBS->CreateEvent (0, TPL_CALLBACK, NULL, NULL, &SelfRemove);
  gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DummyNotify, NULL, &Signal);
  Check (L"2 register plain event", (BOOLEAN)(UefiGuiRegisterEvent (Plain, AppHandler, (VOID *)0) == EFI_SUCCESS));
  Check (L"2 register periodic timer event", (BOOLEAN)(UefiGuiRegisterEvent (Periodic, AppHandler, (VOID *)1) == EFI_SUCCESS));
  Check (L"2 register self-removing event", (BOOLEAN)(UefiGuiRegisterEvent (SelfRemove, AppSelfRemove, NULL) == EFI_SUCCESS));
  Check (L"2 register twice -> EFI_ALREADY_STARTED", (BOOLEAN)(UefiGuiRegisterEvent (Plain, AppHandler, NULL) == EFI_ALREADY_STARTED));
  Check (L"2 register EVT_NOTIFY_SIGNAL -> EFI_INVALID_PARAMETER", (BOOLEAN)(UefiGuiRegisterEvent (Signal, AppHandler, NULL) == EFI_INVALID_PARAMETER));
  Check (L"2 register NULL handler -> EFI_INVALID_PARAMETER", (BOOLEAN)(UefiGuiRegisterEvent (Plain, NULL, NULL) == EFI_INVALID_PARAMETER));

  //
  // One iteration: the signal ends its wait at once and the handler runs
  // before it returns.
  //
  Start = UefiGuiGetTimeUs ();
  gBS->SignalEvent (Plain);
  UefiGuiProcessEvents ();
  End = UefiGuiGetTimeUs () - Start;
  CheckValue (L"2 signaled event: handler ran in the same iteration", (BOOLEAN)(mAppCalls[0] == 1), mAppCalls[0]);
  CheckValue (L"2 signaled event: iteration took us < 5000 (sleep is 1000 ms)", (BOOLEAN)(End < 5000), (INT64)End);
  Pump (100);
  CheckValue (L"2 signaled event: handler not run again", (BOOLEAN)(mAppCalls[0] == 1), mAppCalls[0]);

  gBS->SignalEvent (SelfRemove);
  Pump (50);
  gBS->SignalEvent (SelfRemove);
  Pump (50);
  CheckValue (L"2 handler unregistering itself: ran once", (BOOLEAN)(mAppCalls[3] == 1), mAppCalls[3]);
  Check (L"2 unregister again -> EFI_NOT_FOUND", (BOOLEAN)(UefiGuiUnregisterEvent (SelfRemove) == EFI_NOT_FOUND));

  UefiGuiGetLoopStats (&L0);
  gBS->SetTimer (Periodic, TimerPeriodic, 200 * 10000);
  Pump (1500);
  gBS->SetTimer (Periodic, TimerCancel, 0);
  UefiGuiGetLoopStats (&L1);
  CheckValue (L"2 periodic 200 ms event over 1.5 s: handler 6..8 times", (BOOLEAN)((mAppCalls[1] >= 6) && (mAppCalls[1] <= 8)), mAppCalls[1]);
  CheckValue (L"2 app event wakes", (BOOLEAN)(L1.AppEventWakes - L0.AppEventWakes >= 6), (INT64)(L1.AppEventWakes - L0.AppEventWakes));
  Check (L"2 app handlers ran at TPL_APPLICATION", mAppTplOk);

  Accepted = 2;   // Plain and Periodic are registered.
  for (Index = 0; Index < 8; Index++) {
    gBS->CreateEvent (0, TPL_CALLBACK, NULL, NULL, &Extra[Index]);
    if (UefiGuiRegisterEvent (Extra[Index], AppHandler, (VOID *)2) == EFI_SUCCESS) {
      Accepted++;
    }
  }

  CheckValue (L"2 register limit: 8 events at most", (BOOLEAN)(Accepted == UEFI_GUI_MAX_APP_EVENTS), Accepted);
  for (Index = 0; Index < 8; Index++) {
    UefiGuiUnregisterEvent (Extra[Index]);
    gBS->CloseEvent (Extra[Index]);
  }

  Check (L"2 unregister plain event", (BOOLEAN)(UefiGuiUnregisterEvent (Plain) == EFI_SUCCESS));
  gBS->SignalEvent (Plain);
  Pump (50);
  CheckValue (L"2 unregistered event no longer handled", (BOOLEAN)(mAppCalls[0] == 1), mAppCalls[0]);
  gBS->CloseEvent (Plain);
  gBS->CloseEvent (SelfRemove);
  gBS->CloseEvent (Signal);
  //
  // Periodic stays registered: shutdown must forget it without closing it.
  //

  //
  // Exit from an LVGL timer.
  //
  lv_timer_create (ExitFromTimer, 300, NULL);
  Start  = UefiGuiGetTimeMs ();
  Status = UefiGuiRun ();
  CheckValue (L"2 exit from LVGL timer: UefiGuiRun returned EFI_SUCCESS after ms", (BOOLEAN)((Status == EFI_SUCCESS) && (UefiGuiGetTimeMs () - Start >= 300) && (UefiGuiGetTimeMs () - Start < 400)), (INT64)(UefiGuiGetTimeMs () - Start));

  //
  // Exit from a TPL_CALLBACK notification function; the loop sleeps up to
  // 1000 ms, so only the exit event can end it quickly.
  //
  mExitAtUs = 0;
  Status    = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ExitFromNotify, NULL, &Notify);
  gBS->SetTimer (Notify, TimerRelative, 500 * 10000);
  UefiGuiGetLoopStats (&L0);
  Status = UefiGuiRun ();
  End    = UefiGuiGetTimeUs ();
  UefiGuiGetLoopStats (&L1);
  gBS->CloseEvent (Notify);
  Check (L"2 exit from notify: UefiGuiRun returned EFI_SUCCESS (exit consumed by the first run)", (BOOLEAN)(Status == EFI_SUCCESS));
  CheckValue (L"2 exit from notify: returned within us < 5000 of the request", (BOOLEAN)((mExitAtUs != 0) && (End - mExitAtUs < 5000)), (INT64)(End - mExitAtUs));
  CheckValue (L"2 exit from notify: woken by the exit event", (BOOLEAN)(L1.ExitWakes - L0.ExitWakes == 1), (INT64)(L1.ExitWakes - L0.ExitWakes));

  //
  // Re-entry from inside the loop.
  //
  mReentry = 0;
  lv_timer_create (ReentryCb, 10, NULL);
  Pump (100);
  CheckValue (L"2 re-entry: ProcessEvents, Run denied; Shutdown ignored", (BOOLEAN)(mReentry == 3), mReentry);
  {
    UEFI_GUI_DISPLAY_INFO  Info;

    Check (L"2 re-entry: still initialized after the ignored shutdown", (BOOLEAN)(UefiGuiGetDisplayInfo (&Info) == EFI_SUCCESS));
  }

  //
  // Keyboard: the loop sleeps up to 1000 ms; each key must wake it.
  // Latency includes the host side (serial log polling, QEMU monitor, USB
  // report interval), so the bound is far below the 1000 ms sleep only.
  //
  UefiGuiSetKeyHandler (KeyHandler, NULL);
  mKeysSeen = 0;
  MaxKeyMs  = 0;
  SumKeyMs  = 0;
  Keys      = 0;
  Ok        = TRUE;
  UefiGuiGetLoopStats (&L0);
  for (Index = 0; Index < 5; Index++) {
    Pump (300);
    It0    = mKeysSeen;
    SendAt = UefiGuiGetTimeUs ();
    SendCmd ("sendkey a");
    End = UefiGuiGetTimeMs () + STEP_TIMEOUT_MS;
    while ((mKeysSeen == It0) && (UefiGuiGetTimeMs () < End)) {
      UefiGuiProcessEvents ();
    }

    if (mKeysSeen == It0) {
      Ok = FALSE;
      continue;
    }

    Keys++;
    MaxKeyMs  = MAX (MaxKeyMs, DivU64x32 (mKeyAtUs - SendAt, 1000));
    SumKeyMs += DivU64x32 (mKeyAtUs - SendAt, 1000);
  }

  UefiGuiGetLoopStats (&L1);
  CheckValue (L"2 keyboard: 5 keys seen", (BOOLEAN)(Ok && (Keys == 5)), Keys);
  CheckValue (L"2 keyboard: input wakes >= 5", (BOOLEAN)(L1.InputWakes - L0.InputWakes >= 5), (INT64)(L1.InputWakes - L0.InputWakes));
  CheckValue (L"2 keyboard: max request-to-handler ms < 500 (sleep is 1000)", (BOOLEAN)(MaxKeyMs < 500), (INT64)MaxKeyMs);
  CheckValue (L"2 keyboard: mean request-to-handler ms", (BOOLEAN)(Keys > 0), (Keys > 0) ? (INT64)DivU64x32 (SumKeyMs, Keys) : 0);

  //
  // Exit from the keyboard (Esc in the key handler).
  //
  mEscExit = FALSE;
  SendCmd ("sendkey esc");
  Status = UefiGuiRun ();
  Check (L"2 exit from keyboard (Esc): UefiGuiRun returned EFI_SUCCESS", (BOOLEAN)((Status == EFI_SUCCESS) && mEscExit));

  //
  // Callbacks still queued at shutdown are dropped, never run.
  //
  mOrderNext = 0;
  UefiGuiPostCallback (OrderCb, (VOID *)0);
  UefiGuiPostCallback (OrderCb, (VOID *)1);
  UefiGuiPostCallback (OrderCb, (VOID *)2);
  UefiGuiShutdown ();
  UefiGuiGetLoopStats (&L1);
  CheckValue (L"2 shutdown: 3 queued callbacks dropped", (BOOLEAN)(L1.CallbacksDropped == 3), (INT64)L1.CallbacksDropped);
  CheckValue (L"2 shutdown: dropped callbacks never ran", (BOOLEAN)(mOrderNext == 0), mOrderNext);
  CheckValue (L"2 shutdown: UefiGui memory back to start", (BOOLEAN)(GuiBytes () == Baseline), (INT64)(GuiBytes () - Baseline));
  CheckValue (L"2 shutdown: loop events open", (BOOLEAN)(L1.OpenEvents == 0), L1.OpenEvents);
  Check (L"2 shutdown: registered app event left open for its owner", (BOOLEAN)(gBS->CheckEvent (Periodic) == EFI_NOT_READY));
  gBS->CloseEvent (Periodic);
  Check (L"2 after shutdown: PostCallback -> EFI_NOT_STARTED", (BOOLEAN)(UefiGuiPostCallback (OrderCb, NULL) == EFI_NOT_STARTED));
  Check (L"2 after shutdown: RequestExit -> EFI_NOT_STARTED", (BOOLEAN)(UefiGuiRequestExit () == EFI_NOT_STARTED));
  Check (L"2 after shutdown: ProcessEvents -> EFI_NOT_STARTED", (BOOLEAN)(UefiGuiProcessEvents () == EFI_NOT_STARTED));
}

// ---------------------------------------------------------------------------
// Section 3: timers and input together.
// ---------------------------------------------------------------------------

STATIC UINT32  mTicks20;

STATIC UINT64  mTick20Last;
STATIC UINT64  mTick20MinGap;
STATIC UINT64  mTick20MaxGap;

STATIC
void
Tick20 (
  lv_timer_t  *Timer
  )
{
  UINT64  Now;

  Now = UefiGuiGetTimeMs ();
  if (mTicks20 > 0) {
    mTick20MinGap = MIN (mTick20MinGap, Now - mTick20Last);
    mTick20MaxGap = MAX (mTick20MaxGap, Now - mTick20Last);
  }

  mTick20Last = Now;
  mTicks20++;
}

/**
  Platform timer interrupt period in ms (the resolution of SetTimer()),
  from the Timer Architectural Protocol; 0 if unknown.
**/
STATIC
UINT64
PlatformTickMs (
  VOID
  )
{
  EFI_TIMER_ARCH_PROTOCOL  *TimerArch;
  UINT64                   Period;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiTimerArchProtocolGuid, NULL, (VOID **)&TimerArch))) {
    return 0;
  }

  if (EFI_ERROR (TimerArch->GetTimerPeriod (TimerArch, &Period))) {
    return 0;
  }

  return DivU64x32 (Period + 9999, 10000);
}

STATIC
VOID
SectionTogether (
  IN UINT64  Baseline
  )
{
  EFI_STATUS           Status;
  lv_obj_t             *Text;
  lv_obj_t             *Box;
  lv_timer_t           *Timer;
  lv_anim_t            Anim;
  UINT64               Start;
  UINT64               Elapsed;
  UINT64               End;
  UEFI_GUI_INPUT_STATS  In;
  CONST CHAR8          *Expected;
  UINT64               TickMs;

  Status = InitWith (0, 0, 0);
  Check (L"3 init (defaults)", (BOOLEAN)!EFI_ERROR (Status));
  if (EFI_ERROR (Status)) {
    return;
  }

  Text = lv_textarea_create (lv_screen_active ());
  lv_obj_set_size (Text, 600, 60);
  lv_obj_align (Text, LV_ALIGN_TOP_MID, 0, 40);
  lv_textarea_set_one_line (Text, true);
  lv_group_focus_obj (Text);
  Box = lv_obj_create (lv_screen_active ());
  lv_obj_set_size (Box, 60, 60);
  lv_obj_set_pos (Box, 40, 300);

  lv_anim_init (&Anim);
  lv_anim_set_var (&Anim, Box);
  lv_anim_set_exec_cb (&Anim, AnimX);
  lv_anim_set_values (&Anim, 40, 900);
  lv_anim_set_duration (&Anim, 700);
  lv_anim_set_reverse_duration (&Anim, 700);
  lv_anim_set_repeat_count (&Anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start (&Anim);

  mTicks20      = 0;
  mTick20MinGap = MAX_UINT64;
  mTick20MaxGap = 0;
  TickMs        = PlatformTickMs ();
  CheckValue (L"3 platform timer tick ms (SetTimer resolution)", (BOOLEAN)((TickMs > 0) && (TickMs <= 20)), (INT64)TickMs);
  Timer = lv_timer_create (Tick20, 20, NULL);
  Start    = UefiGuiGetTimeMs ();
  Expected = "hello world 0123456789";
  SendCmd (
    "sendkey h;sendkey e;sendkey l;sendkey l;sendkey o;sendkey spc;"
    "sendkey w;sendkey o;sendkey r;sendkey l;sendkey d;sendkey spc;"
    "sendkey 0;sendkey 1;sendkey 2;sendkey 3;sendkey 4;sendkey 5;sendkey 6;sendkey 7;sendkey 8;sendkey 9"
    );
  End = Start + STEP_TIMEOUT_MS;
  while ((AsciiStrCmp (lv_textarea_get_text (Text), Expected) != 0) && (UefiGuiGetTimeMs () < End)) {
    UefiGuiProcessEvents ();
  }

  Pump (300);
  Elapsed = UefiGuiGetTimeMs () - Start;
  lv_timer_delete (Timer);
  UefiGuiGetInputStats (&In);
  Check (L"3 typed text arrived complete while timer and animation ran", (BOOLEAN)(AsciiStrCmp (lv_textarea_get_text (Text), Expected) == 0));
  CheckValue (L"3 no input event dropped", (BOOLEAN)(In.DroppedEvents == 0), (INT64)In.DroppedEvents);
  //
  // LVGL measures a timer's period from its previous run, and the loop can
  // only wake on a platform tick, so each run is 0..TickMs late and the
  // rate is at least 20 / (20 + TickMs) of nominal.
  //
  CheckValue (L"3 20 ms timer while typing: shortest gap >= 20 ms", (BOOLEAN)(mTick20MinGap >= 20), (INT64)mTick20MinGap);
  CheckValue (L"3 20 ms timer while typing: longest gap <= 20 + tick + 10 ms", (BOOLEAN)(mTick20MaxGap <= 30 + TickMs), (INT64)mTick20MaxGap);
  CheckValue (
    L"3 20 ms timer while typing: per mille of nominal rate >= 20/(20+tick)",
    (BOOLEAN)((UINT64)mTicks20 * (20 + TickMs) * 1000 >= Elapsed * 1000 - 50 * Elapsed),
    (INT64)DivU64x64Remainder ((UINT64)mTicks20 * 20 * 1000, Elapsed, NULL)
    );
  SendCmd ("screenshot");
  Pump (2500);

  ShutdownAndCheck (L"3 shutdown: UefiGui memory back to start", L"3 shutdown: loop events open", Baseline);
}

// ---------------------------------------------------------------------------
// Section 4: cleanup.
// ---------------------------------------------------------------------------

/**
  Free conventional memory in pages, from the UEFI memory map.
**/
STATIC
UINT64
FreeConventionalPages (
  VOID
  )
{
  EFI_STATUS             Status;
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINT64                 Pages;

  Size   = 0;
  Map    = NULL;
  Status = gBS->GetMemoryMap (&Size, NULL, &Key, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return 0;
  }

  Size += 8 * DescSize;
  Map   = AllocatePool (Size);
  if (Map == NULL) {
    return 0;
  }

  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVersion);
  Pages  = 0;
  if (!EFI_ERROR (Status)) {
    for (Desc = Map; (UINT8 *)Desc < (UINT8 *)Map + Size; Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescSize)) {
      if (Desc->Type == EfiConventionalMemory) {
        Pages += Desc->NumberOfPages;
      }
    }
  }

  FreePool (Map);
  return Pages;
}

STATIC
VOID
SectionCleanup (
  IN UINT64  Baseline
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UINT32                        OriginalWidth;
  UEFI_GUI_DISPLAY_INFO         Info;
  UINT64                        PagesBefore;
  UINT64                        PagesAfter;
  UINT32                        Cycle;
  BOOLEAN                       Ok;
  lv_obj_t                      *Obj;
  UINT32                        Index;

  Gop           = NULL;
  OriginalWidth = 0;
  gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (Gop != NULL) {
    OriginalWidth = Gop->Mode->Info->HorizontalResolution;
  }

  //
  // A session that changes the GOP mode restores it.
  //
  Status = InitWith (0, 800, 600);
  Check (L"4 init with 800 x 600", (BOOLEAN)!EFI_ERROR (Status));
  if (!EFI_ERROR (Status)) {
    UefiGuiGetDisplayInfo (&Info);
    CheckValue (L"4 display width while running", (BOOLEAN)(Info.HorizontalResolution == 800), Info.HorizontalResolution);
    lv_label_set_text (lv_label_create (lv_screen_active ()), "800 x 600");
    Pump (200);
    ShutdownAndCheck (L"4 shutdown: UefiGui memory back to start", L"4 shutdown: loop events open", Baseline);
  }

  CheckValue (
    L"4 GOP mode restored (width)",
    (BOOLEAN)((Gop != NULL) && (Gop->Mode->Info->HorizontalResolution == OriginalWidth)),
    (Gop != NULL) ? Gop->Mode->Info->HorizontalResolution : 0
    );

  //
  // Five sessions, each building widgets and running briefly. The first
  // warms up firmware pools; the free page count must not drop after it.
  //
  Ok          = TRUE;
  PagesBefore = 0;
  for (Cycle = 0; Cycle < 5; Cycle++) {
    if (Cycle == 1) {
      PagesBefore = FreeConventionalPages ();
    }

    Status = InitWith (0, 0, 0);
    if (EFI_ERROR (Status)) {
      Ok = FALSE;
      break;
    }

    for (Index = 0; Index < 50; Index++) {
      Obj = lv_button_create (lv_screen_active ());
      lv_obj_set_pos (Obj, (int32_t)(10 + (Index % 10) * 100), (int32_t)(10 + (Index / 10) * 60));
      lv_label_set_text (lv_label_create (Obj), "Button");
    }

    Pump (150);
    UefiGuiShutdown ();
    if (GuiBytes () != Baseline) {
      Ok = FALSE;
    }
  }

  PagesAfter = FreeConventionalPages ();
  CheckValue (L"4 five init/shutdown cycles: UefiGui memory back to start each time", Ok, (INT64)(GuiBytes () - Baseline));
  CheckValue (L"4 five cycles: free pages lost after warm-up", (BOOLEAN)(PagesAfter >= PagesBefore), (INT64)(PagesBefore - PagesAfter));
}

EFI_STATUS
EFIAPI
RuntimeTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT64  Baseline;
  UINTN   Index;
  UINTN   Failures;

  Print (L"RuntimeTest - LVGLUefiGuiPkg Phase 5\n");
  gBS->LocateProtocol (&gEfiSerialIoProtocolGuid, NULL, (VOID **)&mSerial);
  Check (L"serial port for host commands", (BOOLEAN)(mSerial != NULL));

  Baseline = GuiBytes ();
  SectionIdleAndTimers (Baseline);
  SectionWakeSources (Baseline);
  SectionTogether (Baseline);
  SectionCleanup (Baseline);

  Failures = 0;
  for (Index = 0; Index < mResultCount; Index++) {
    if (!mResults[Index].Passed) {
      Failures++;
    }

    if (mResults[Index].HasValue) {
      Print (L"  [%s] %s: %ld\n", mResults[Index].Passed ? L"PASS" : L"FAIL", mResults[Index].Name, mResults[Index].Value);
    } else {
      Print (L"  [%s] %s\n", mResults[Index].Passed ? L"PASS" : L"FAIL", mResults[Index].Name);
    }
  }

  Print (L"%lu checks, %lu failed\n", (UINT64)mResultCount, (UINT64)Failures);
  Print (L"UEFIGUI-RUNTIMETEST: %s\n", (Failures == 0) ? L"PASS" : L"FAIL");
  return (Failures == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
