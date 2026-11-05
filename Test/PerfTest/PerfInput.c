/** @file
  PerfTest section 3: input response latency.

  Injected events: a one-shot EVT_TIMER notification at TPL_CALLBACK stands
  in for a firmware input device. It stamps T0 and posts a callback; the
  callback (GUI context) queues the key or pointer event. This is the same
  path as a real device: an event signaled from a notification wakes
  WaitForEvent(), the loop reads the queued input, LVGL dispatches it and
  the display refresh timer redraws. Measured:
    handler: T0 -> LVGL event handler (VALUE_CHANGED / PRESSED),
    pixels:  T0 -> end of the first refresh after the handler
             (LV_EVENT_REFR_READY), i.e. the change is on the screen.
  Keys go into a focused text area (its cursor blink keeps the loop busy);
  clicks go to a button while nothing animates (the loop sleeps, so the
  wake-up from idle is included).

  USB keyboard (reference): the host types through the QEMU monitor. The
  time from the serial request to the handler includes the host driver's
  polling and QEMU, so it is an upper bound, not the framework latency.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "PerfTest.h"

#define INJECT_SAMPLES   40
#define HOST_SAMPLES     8
#define SAMPLE_LIMIT_MS  2000

typedef enum {
  InjectKey,
  InjectPress,
  InjectRelease
} INJECT_KIND;

STATIC volatile UINT64  mT0;
STATIC volatile UINT64  mT1;
STATIC volatile UINT64  mT2;
STATIC INJECT_KIND      mKind;
STATIC UINT32           mCharIndex;
STATIC INT32            mClickX;
STATIC INT32            mClickY;
STATIC UINT32           mPostFailures;
STATIC UINT32           mEnqueueFailures;
STATIC UINT32           mNoT0;
STATIC UINT32           mNoT1;
STATIC UINT32           mNoT2;

STATIC PERF_SAMPLES  mHandler;
STATIC PERF_SAMPLES  mPixels;

STATIC
VOID
EFIAPI
InjectCallback (
  IN VOID  *Context
  )
{
  UEFI_GUI_INPUT_EVENT  Event;
  BOOLEAN               Ok;

  ZeroMem (&Event, sizeof (Event));
  if (mKind == InjectKey) {
    Event.Type                = UEFI_GUI_INPUT_KEY;
    Event.u.Key.ScanCode      = SCAN_NULL;
    Event.u.Key.UnicodeChar   = (CHAR16)(L'a' + (mCharIndex++ % 26));
    Event.u.Key.Pressed       = TRUE;
    Ok = UefiGuiInputEnqueue (&Event);
    //
    // UEFI keyboards report presses only; the input backend queues the
    // matching release right after each press. Do the same.
    //
    Event.u.Key.Pressed = FALSE;
    Ok                  = (BOOLEAN)(Ok && UefiGuiInputEnqueue (&Event));
  } else {
    Event.Type     = UEFI_GUI_INPUT_POINTER_MOVE;
    Event.u.Move.X = mClickX;
    Event.u.Move.Y = mClickY;
    Ok             = UefiGuiInputEnqueue (&Event);
    ZeroMem (&Event, sizeof (Event));
    Event.Type             = UEFI_GUI_INPUT_POINTER_BUTTON;
    Event.u.Button.Button  = UEFI_GUI_POINTER_BUTTON_LEFT;
    Event.u.Button.Pressed = (BOOLEAN)(mKind == InjectPress);
    Ok                     = (BOOLEAN)(Ok && UefiGuiInputEnqueue (&Event));
  }

  if (!Ok) {
    mEnqueueFailures++;
  }
}

STATIC
VOID
EFIAPI
InjectNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mT0 = UefiGuiGetTimeUs ();
  if (EFI_ERROR (UefiGuiPostCallback (InjectCallback, NULL))) {
    mPostFailures++;
  }
}

STATIC
VOID
HandlerEvent (
  lv_event_t  *e
  )
{
  if (mT1 == 0) {
    mT1 = UefiGuiGetTimeUs ();
  }
}

STATIC
VOID
RefreshReady (
  lv_event_t  *e
  )
{
  if ((mT1 != 0) && (mT2 == 0)) {
    mT2 = UefiGuiGetTimeUs ();
  }
}

/**
  Fire the injection timer after a random delay and run the loop until the
  change is on the screen.

  @retval TRUE  Both stamps were taken.
**/
STATIC
BOOLEAN
InjectOnce (
  IN EFI_EVENT    Timer,
  IN INJECT_KIND  Kind
  )
{
  UINT64  End;

  mKind = Kind;
  mT0   = 0;
  mT1   = 0;
  mT2   = 0;
  gBS->SetTimer (Timer, TimerRelative, (UINT64)(20 + Random32 () % 60) * 10000);
  End = UefiGuiGetTimeMs () + SAMPLE_LIMIT_MS;
  while ((mT2 == 0) && (UefiGuiGetTimeMs () < End)) {
    if (UefiGuiProcessEvents () != EFI_SUCCESS) {
      break;
    }
  }

  if (mT0 == 0) {
    mNoT0++;
  } else if (mT1 == 0) {
    mNoT1++;
  } else if (mT2 == 0) {
    mNoT2++;
  }

  return (BOOLEAN)((mT0 != 0) && (mT1 >= mT0) && (mT2 >= mT1));
}

VOID
SectionInputLatency (
  VOID
  )
{
  EFI_STATUS             Status;
  EFI_EVENT              Timer;
  PERF_SCENE             Scene;
  PERF_SUMMARY           Pixels;
  UINT64                 Baseline;
  UINT64                 Start;
  UINT64                 End;
  UINTN                  Index;
  UINTN                  Missing;
  UEFI_GUI_INPUT_STATS   InputStats;
  UEFI_GUI_LOOP_STATS    Loop;
  lv_area_t              Area;

  Baseline = GuiBytes ();
  if (EFI_ERROR (InitGui (UefiGuiDisplayBackendAuto, 0, 0, 0))) {
    Check (L"3 UefiGuiInitialize", FALSE);
    return;
  }

  BuildScene (&Scene);
  lv_obj_add_event_cb (Scene.TextArea, HandlerEvent, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb (Scene.Button[2], HandlerEvent, LV_EVENT_PRESSED, NULL);
  lv_display_add_event_cb (lv_display_get_default (), RefreshReady, LV_EVENT_REFR_READY, NULL);
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, InjectNotify, NULL, &Timer);
  Check (L"3 injection timer created", (BOOLEAN)!EFI_ERROR (Status));
  if (EFI_ERROR (Status)) {
    UefiGuiShutdown ();
    return;
  }

  //
  // Keys into the focused text area.
  //
  lv_group_focus_obj (Scene.TextArea);
  Pump (300);
  mHandler.Count = 0;
  mPixels.Count  = 0;
  Missing        = 0;
  for (Index = 0; Index < INJECT_SAMPLES; Index++) {
    if ((Index % 30) == 29) {
      lv_textarea_set_text (Scene.TextArea, "");
      Pump (50);
    }

    if (InjectOnce (Timer, InjectKey)) {
      SampleAdd (&mHandler, mT1 - mT0);
      SampleAdd (&mPixels, mT2 - mT0);
    } else {
      Missing++;
    }
  }

  Metric ("input.key.missing_injection", mNoT0, "n");
  Metric ("input.key.missing_handler", mNoT1, "n");
  Metric ("input.key.missing_refresh", mNoT2, "n");
  CheckValue (L"3 every injected key reached the text area and the screen", (BOOLEAN)(Missing == 0), (INT64)Missing);
  MetricSummary ("input.key.handler", &mHandler, NULL);
  MetricSummary ("input.key.pixels", &mPixels, &Pixels);
  CheckValue (L"3 key to screen: p95 below 100 ms", (BOOLEAN)((mPixels.Count > 0) && (Pixels.P95 < 100000)), (INT64)Pixels.P95);

  //
  // Clicks on a button while the screen is idle (focus moved away from the
  // text area so its cursor stops blinking).
  //
  if ((UefiGuiGetInputDevices () & (UEFI_GUI_INPUT_DEVICE_SIMPLE_POINTER | UEFI_GUI_INPUT_DEVICE_ABSOLUTE_POINTER)) != 0) {
    lv_group_focus_obj (Scene.Button[0]);
    ZeroMem (&Area, sizeof (Area));
    lv_obj_get_coords (Scene.Button[2], &Area);
    mClickX = (Area.x1 + Area.x2) / 2;
    mClickY = (Area.y1 + Area.y2) / 2;
    Pump (800);
    mHandler.Count = 0;
    mPixels.Count  = 0;
    Missing        = 0;
    for (Index = 0; Index < INJECT_SAMPLES; Index++) {
      Pump (150);
      if (InjectOnce (Timer, InjectPress)) {
        SampleAdd (&mHandler, mT1 - mT0);
        SampleAdd (&mPixels, mT2 - mT0);
      } else {
        Missing++;
      }

      mKind = InjectRelease;
      InjectCallback (NULL);
      Pump (100);
    }

    CheckValue (L"3 every injected click reached the button and the screen", (BOOLEAN)(Missing == 0), (INT64)Missing);
    MetricSummary ("input.click.handler", &mHandler, NULL);
    MetricSummary ("input.click.pixels", &mPixels, &Pixels);
    CheckValue (L"3 click from idle to screen: p95 below 100 ms", (BOOLEAN)((mPixels.Count > 0) && (Pixels.P95 < 100000)), (INT64)Pixels.P95);
  } else {
    Check (L"3 pointer device present for click latency (run with --pointer mouse --load UsbMouseDxe.efi)", FALSE);
  }

  CheckValue (L"3 no posted callback refused", (BOOLEAN)(mPostFailures == 0), mPostFailures);
  CheckValue (L"3 no queued event refused", (BOOLEAN)(mEnqueueFailures == 0), mEnqueueFailures);

  //
  // USB keyboard typed by the host (reference, includes host latency).
  //
  lv_group_focus_obj (Scene.TextArea);
  Pump (200);
  mHandler.Count = 0;
  Missing        = 0;
  for (Index = 0; Index < HOST_SAMPLES; Index++) {
    mT1   = 0;
    Start = UefiGuiGetTimeUs ();
    SendCmd ("sendkey x");
    End = UefiGuiGetTimeMs () + 3000;
    while ((mT1 == 0) && (UefiGuiGetTimeMs () < End)) {
      UefiGuiProcessEvents ();
    }

    if (mT1 != 0) {
      SampleAdd (&mHandler, mT1 - Start);
    } else {
      Missing++;
    }

    Pump (200 + Random32 () % 200);
  }

  CheckValue (L"3 every host key press arrived", (BOOLEAN)(Missing == 0), (INT64)Missing);
  MetricSummary ("input.host_usb_key.request_to_handler", &mHandler, NULL);

  UefiGuiGetInputStats (&InputStats);
  CheckValue (L"3 no input event dropped", (BOOLEAN)(InputStats.DroppedEvents == 0), (INT64)InputStats.DroppedEvents);
  gBS->CloseEvent (Timer);
  UefiGuiShutdown ();
  ZeroMem (&Loop, sizeof (Loop));
  UefiGuiGetLoopStats (&Loop);
  Metric ("input.loop.longest_iteration", Loop.LongestIterationUs, "us");
  CheckValue (L"3 memory back to baseline", (BOOLEAN)(GuiBytes () == Baseline), (INT64)(GuiBytes () - Baseline));
}
