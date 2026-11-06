/** @file
  PerfTest section 5: long-running stability (soak).

  For a fixed time the screen runs a spinner and an endless bar animation,
  a 200 ms LVGL timer deletes and recreates a dozen widgets and updates the
  chart, table and status label, every tenth cycle forces a full refresh,
  and a 40 ms TPL_CALLBACK timer injects a key press (every tick) and a
  pointer move (every third tick) through UefiGuiPostCallback(). Every
  5 seconds the test brings the screen to the same state (churn container
  and text area emptied) and samples the memory in use, so a leak shows up
  as growth between samples.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "PerfTest.h"

#define SOAK_SAMPLE_MS  5000
#define SOAK_TICK_MS    40
#define MAX_MEM_SAMPLES 1024

STATIC PERF_SCENE  mScene;
STATIC UINT64      mTicks;
STATIC UINT64      mEnqueuedKeys;
STATIC UINT64      mEnqueuedMoves;
STATIC UINT64      mDeliveredKeys;
STATIC UINT64      mInsertedChars;
STATIC UINT64      mPostRejected;
STATIC UINT64      mEnqueueRejected;
STATIC UINT64      mChurnCycles;
STATIC UINT64      mObjectsCreated;
STATIC UINT64      mMemSample[MAX_MEM_SAMPLES];
STATIC UINTN       mMemSampleCount;
STATIC UINT64      mFrameSample[MAX_MEM_SAMPLES];
STATIC UINT64      mBusySample[MAX_MEM_SAMPLES];
STATIC UINT64      mWaitSample[MAX_MEM_SAMPLES];
STATIC UINT64      mLongestSample[MAX_MEM_SAMPLES];
STATIC UINT32      mWidth;
STATIC UINT32      mHeight;
STATIC lv_chart_series_t  *mSeries;

STATIC
VOID
EFIAPI
SoakInput (
  IN VOID  *Context
  )
{
  UEFI_GUI_INPUT_EVENT  Event;
  UINTN                 Tick;

  Tick = (UINTN)Context;
  ZeroMem (&Event, sizeof (Event));
  Event.Type              = UEFI_GUI_INPUT_KEY;
  Event.u.Key.ScanCode    = SCAN_NULL;
  Event.u.Key.UnicodeChar = (CHAR16)(L'a' + Tick % 26);
  Event.u.Key.Pressed     = TRUE;
  if (UefiGuiInputEnqueue (&Event)) {
    mEnqueuedKeys++;
    //
    // Same as the keyboard backend: a release follows every press.
    //
    Event.u.Key.Pressed = FALSE;
    if (!UefiGuiInputEnqueue (&Event)) {
      mEnqueueRejected++;
    }
  } else {
    mEnqueueRejected++;
  }

  if ((Tick % 3) == 0) {
    ZeroMem (&Event, sizeof (Event));
    Event.Type     = UEFI_GUI_INPUT_POINTER_MOVE;
    Event.u.Move.X = (INT32)(Random32 () % mWidth);
    Event.u.Move.Y = (INT32)(Random32 () % mHeight);
    if (UefiGuiInputEnqueue (&Event)) {
      mEnqueuedMoves++;
    } else {
      mEnqueueRejected++;
    }
  }
}

STATIC
VOID
EFIAPI
SoakTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mTicks++;
  if (EFI_ERROR (UefiGuiPostCallback (SoakInput, (VOID *)(UINTN)mTicks))) {
    mPostRejected++;
  }
}

STATIC
BOOLEAN
EFIAPI
SoakKeyHandler (
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN VOID                      *Context
  )
{
  mDeliveredKeys++;
  return FALSE;
}

/**
  LV_EVENT_INSERT of the text area: one character typed by a key.
**/
STATIC
VOID
SoakInsert (
  lv_event_t  *e
  )
{
  CONST CHAR8  *Text;

  Text = (CONST CHAR8 *)lv_event_get_param (e);
  if ((Text != NULL) && (Text[0] >= 'a') && (Text[0] <= 'z') && (Text[1] == '\0')) {
    mInsertedChars++;
  }
}

STATIC
VOID
SoakChurn (
  lv_timer_t  *Timer
  )
{
  UINTN     Index;
  lv_obj_t  *Obj;

  mChurnCycles++;
  lv_obj_clean (mScene.Churn);
  for (Index = 0; Index < 12; Index++) {
    switch (Index % 3) {
      case 0:
        Obj = lv_label_create (mScene.Churn);
        lv_label_set_text_fmt (Obj, "L%02u", (UINT32)Index);
        break;
      case 1:
        Obj = lv_button_create (mScene.Churn);
        lv_label_set_text (lv_label_create (Obj), "Btn");
        break;
      default:
        Obj = lv_checkbox_create (mScene.Churn);
        lv_checkbox_set_text (Obj, "Chk");
        break;
    }

    mObjectsCreated++;
  }

  lv_chart_set_next_value (mScene.Chart, mSeries, (INT32)(Random32 () % 100));
  lv_table_set_cell_value_fmt (mScene.Table, 0, 1, "%05u", (UINT32)(mChurnCycles % 100000));
  lv_label_set_text_fmt (mScene.Status, "soak: cycle %08u, keys %08u", (UINT32)mChurnCycles, (UINT32)mDeliveredKeys);
  if (lv_textarea_get_text (mScene.TextArea) != NULL) {
    if (AsciiStrLen (lv_textarea_get_text (mScene.TextArea)) > 300) {
      lv_textarea_set_text (mScene.TextArea, "");
    }
  }

  if ((mChurnCycles % 10) == 0) {
    lv_obj_invalidate (lv_screen_active ());
  }
}

STATIC
VOID
BarAnim (
  VOID     *Var,
  int32_t  Value
  )
{
  lv_bar_set_value ((lv_obj_t *)Var, Value, LV_ANIM_OFF);
}

VOID
SectionSoak (
  IN UINT32  Seconds
  )
{
  EFI_STATUS             Status;
  EFI_EVENT              Tick;
  UEFI_GUI_DISPLAY_INFO  Info;
  UEFI_GUI_RENDER_STATS  Render;
  UEFI_GUI_INPUT_STATS   Input;
  UEFI_GUI_LOOP_STATS    Loop;
  lv_timer_t             *Churn;
  lv_obj_t               *Spinner;
  lv_anim_t              Anim;
  UINT64                 Baseline;
  UINT64                 FreeBefore;
  UINT64                 FreeAfter;
  UINT64                 Start;
  UINT64                 End;
  UINT64                 NextSample;
  UINT64                 FirstMax;
  UINT64                 SecondMax;
  UINT64                 Growth;
  UINTN                  Index;
  UINTN                  Half;

  Baseline   = GuiBytes ();
  FreeBefore = FreeSystemKib ();
  if (EFI_ERROR (InitGui (UefiGuiDisplayBackendAuto, 0, 0, 0))) {
    Check (L"5 UefiGuiInitialize", FALSE);
    return;
  }

  UefiGuiGetDisplayInfo (&Info);
  mWidth  = Info.HorizontalResolution;
  mHeight = Info.VerticalResolution;
  BuildScene (&mScene);
  mSeries = lv_chart_get_series_next (mScene.Chart, NULL);
  lv_group_focus_obj (mScene.TextArea);
  lv_obj_add_event_cb (mScene.TextArea, SoakInsert, LV_EVENT_INSERT, NULL);

  Spinner = lv_spinner_create (lv_screen_active ());
  lv_obj_set_size (Spinner, 48, 48);
  lv_obj_align (Spinner, LV_ALIGN_TOP_RIGHT, -24, 16);

  lv_anim_init (&Anim);
  lv_anim_set_var (&Anim, mScene.Bar);
  lv_anim_set_exec_cb (&Anim, BarAnim);
  lv_anim_set_values (&Anim, 0, 100);
  lv_anim_set_duration (&Anim, 1500);
  lv_anim_set_reverse_duration (&Anim, 1500);
  lv_anim_set_repeat_count (&Anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start (&Anim);

  UefiGuiSetKeyHandler (SoakKeyHandler, NULL);
  Churn  = lv_timer_create (SoakChurn, 200, NULL);
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, SoakTick, NULL, &Tick);
  Check (L"5 input timer created", (BOOLEAN)!EFI_ERROR (Status));
  if (EFI_ERROR (Status)) {
    lv_timer_delete (Churn);
    UefiGuiShutdown ();
    return;
  }

  Pump (500);
  gBS->SetTimer (Tick, TimerPeriodic, SOAK_TICK_MS * 10000);

  Start      = UefiGuiGetTimeMs ();
  End        = Start + (UINT64)Seconds * 1000;
  NextSample = Start + SOAK_SAMPLE_MS;
  while (UefiGuiGetTimeMs () < End) {
    if (UefiGuiProcessEvents () != EFI_SUCCESS) {
      break;
    }

    if (UefiGuiGetTimeMs () >= NextSample) {
      NextSample += SOAK_SAMPLE_MS;
      //
      // Same state at every sample: no churn widgets, empty text area.
      //
      lv_obj_clean (mScene.Churn);
      lv_textarea_set_text (mScene.TextArea, "");
      if (mMemSampleCount < MAX_MEM_SAMPLES) {
        UefiGuiGetRenderStats (&Render);
        UefiGuiGetLoopStats (&Loop);
        mFrameSample[mMemSampleCount]   = Render.FrameCount;
        mBusySample[mMemSampleCount]    = Loop.BusyTimeUs;
        mWaitSample[mMemSampleCount]    = Loop.WaitTimeUs;
        mLongestSample[mMemSampleCount] = Loop.LongestIterationUs;
        mMemSample[mMemSampleCount++]   = GuiBytes ();
      }
    }
  }

  gBS->SetTimer (Tick, TimerCancel, 0);
  gBS->CloseEvent (Tick);
  Pump (500);

  UefiGuiGetRenderStats (&Render);
  UefiGuiGetInputStats (&Input);
  UefiGuiGetLoopStats (&Loop);

  Metric ("soak.seconds", (UefiGuiGetTimeMs () - Start) / 1000, "s");
  Metric ("soak.injected_keys", mEnqueuedKeys, "n");
  Metric ("soak.injected_moves", mEnqueuedMoves, "n");
  Metric ("soak.delivered_keys", mDeliveredKeys, "n");
  Metric ("soak.inserted_chars", mInsertedChars, "n");
  Metric ("soak.churn_cycles", mChurnCycles, "n");
  Metric ("soak.objects_created", mObjectsCreated, "n");
  Metric ("soak.frames", Render.FrameCount, "n");
  Metric ("soak.flushes", Render.FlushCount, "n");
  Metric ("soak.flushed_mpixels", DivU64x64Remainder (Render.FlushedPixels, 1000000, NULL), "Mpx");
  Metric ("soak.flush_errors", Render.FlushErrors, "n");
  Metric ("soak.loop_iterations", Loop.Iterations, "n");
  Metric ("soak.loop_longest_iteration", Loop.LongestIterationUs, "us");
  if (Loop.WaitTimeUs + Loop.BusyTimeUs != 0) {
    Metric ("soak.loop_busy_percent", DivU64x64Remainder (Loop.BusyTimeUs * 100, Loop.WaitTimeUs + Loop.BusyTimeUs, NULL), "%");
  }

  Metric ("soak.memory_samples", mMemSampleCount, "n");
  Growth = 0;
  if (mMemSampleCount >= 4) {
    //
    // Skip the first sample (caches warming up), then compare the halves.
    //
    Half      = 1 + (mMemSampleCount - 1) / 2;
    FirstMax  = 0;
    SecondMax = 0;
    for (Index = 1; Index < mMemSampleCount; Index++) {
      if (Index < Half) {
        FirstMax = MAX (FirstMax, mMemSample[Index]);
      } else {
        SecondMax = MAX (SecondMax, mMemSample[Index]);
      }
    }

    Growth = (SecondMax > FirstMax) ? SecondMax - FirstMax : 0;
    Metric ("soak.memory_first_sample", mMemSample[0] - Baseline, "B");
    Metric ("soak.memory_last_sample", mMemSample[mMemSampleCount - 1] - Baseline, "B");
  }

  Metric ("soak.memory_growth", Growth, "B");
  if (mMemSampleCount >= 3) {
    //
    // Frame rate and loop load of the second and of the last interval
    // (the first one includes the warm-up), to show any slow-down.
    //
    Metric ("soak.fps_start", DivU64x64Remainder ((mFrameSample[1] - mFrameSample[0]) * 1000, SOAK_SAMPLE_MS, NULL), "fps");
    Metric (
      "soak.fps_end",
      DivU64x64Remainder ((mFrameSample[mMemSampleCount - 1] - mFrameSample[mMemSampleCount - 2]) * 1000, SOAK_SAMPLE_MS, NULL),
      "fps"
      );
  }
  CheckValue (L"5 soak ran for the requested time", (BOOLEAN)(UefiGuiGetTimeMs () - Start + 1000 >= (UINT64)Seconds * 1000), (INT64)((UefiGuiGetTimeMs () - Start) / 1000));
  CheckValue (L"5 memory samples taken", (BOOLEAN)(mMemSampleCount >= 2), (INT64)mMemSampleCount);
  CheckValue (L"5 no memory growth between the halves of the soak", (BOOLEAN)(Growth == 0), (INT64)Growth);
  CheckValue (L"5 every injected key delivered", (BOOLEAN)((mEnqueuedKeys > 0) && (mDeliveredKeys == mEnqueuedKeys)), (INT64)(mEnqueuedKeys - mDeliveredKeys));
  CheckValue (L"5 every injected key typed one character into the text area", (BOOLEAN)(mInsertedChars == mEnqueuedKeys), (INT64)(mEnqueuedKeys - mInsertedChars));
  CheckValue (L"5 no posted callback refused", (BOOLEAN)(mPostRejected == 0), (INT64)mPostRejected);
  CheckValue (L"5 no queued event refused or dropped", (BOOLEAN)((mEnqueueRejected == 0) && (Input.DroppedEvents == 0)), (INT64)(mEnqueueRejected + Input.DroppedEvents));
  CheckValue (L"5 no flush errors", (BOOLEAN)(Render.FlushErrors == 0), (INT64)Render.FlushErrors);
  CheckValue (L"5 the screen kept refreshing (at least 5 frames per second)", (BOOLEAN)(Render.FrameCount >= (UINT64)Seconds * 5), (INT64)Render.FrameCount);
  CheckValue (L"5 widget churn kept running (at least 3 cycles per second)", (BOOLEAN)(mChurnCycles >= (UINT64)Seconds * 3), (INT64)mChurnCycles);

  lv_timer_delete (Churn);
  UefiGuiSetKeyHandler (NULL, NULL);
  UefiGuiShutdown ();
  ZeroMem (&Loop, sizeof (Loop));
  UefiGuiGetLoopStats (&Loop);
  CheckValue (L"5 no UEFI event left open", (BOOLEAN)(Loop.OpenEvents == 0), Loop.OpenEvents);
  CheckValue (L"5 memory back to baseline after shutdown", (BOOLEAN)(GuiBytes () == Baseline), (INT64)(GuiBytes () - Baseline));
  //
  // Per-interval trace, printed after shutdown so it never draws over the
  // GUI: seconds, frames, busy %, longest iteration so far, bytes in use.
  //
  for (Index = 1; Index < mMemSampleCount; Index++) {
    Print (
      L"UEFIGUI-SOAK-TRACE %lu %lu %lu %lu %lu\n",
      (UINT64)(Index * SOAK_SAMPLE_MS / 1000),
      mFrameSample[Index] - mFrameSample[Index - 1],
      DivU64x64Remainder (
        (mBusySample[Index] - mBusySample[Index - 1]) * 100,
        MAX (1, (mBusySample[Index] - mBusySample[Index - 1]) + (mWaitSample[Index] - mWaitSample[Index - 1])),
        NULL
        ),
      mLongestSample[Index],
      mMemSample[Index]
      );
  }

  FreeAfter = FreeSystemKib ();
  Metric ("soak.system_free_delta", (FreeBefore >= FreeAfter) ? FreeBefore - FreeAfter : 0, "KiB");
}
