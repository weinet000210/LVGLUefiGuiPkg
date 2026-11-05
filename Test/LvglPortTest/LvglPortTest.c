/** @file
  LvglPortTest - Phase 2 test for the LVGL v9 port.

  Builds a small screen (title, Button with Label, click counter, slider,
  spinner, monospace text, a color probe) and checks:
    - configuration defaults and validation, API calls before initialization
    - the time base against gBS->Stall()
    - LVGL output reaches GOP with correct colors (read back through GOP)
    - partial rendering: a full refresh is split into draw-buffer bands that
      add up to exactly the screen, a small change flushes a small area
    - button event handling (LV_EVENT_CLICKED updates the label)
    - flush completion: injected flush failures are reported and LVGL keeps
      rendering (lv_display_flush_ready() is called on the error path)
    - the event loop: LVGL timers run, the loop sleeps instead of spinning,
      and UefiGuiRequestExit() from an LVGL timer ends UefiGuiRun()
    - shutdown releases every byte, and LVGL can be initialized again

  Results are collected while the GUI owns the screen and printed after
  shutdown. The last line is "UEFIGUI-LVGLPORT: PASS" or "... FAIL".

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/LvglLib.h>
#include <Library/UefiGuiDisplayLib.h>
#include <Library/UefiGuiLib.h>
#include <Library/UefiGuiRuntimeLib.h>

#define DRAW_LINES      40
#define RUN_MS          6000
#define SCREEN_COLOR    0x1E2530
#define PROBE_COLOR     0xE04010
#define PROBE_SIZE      40
#define MAX_RESULTS     64

typedef struct {
  CONST CHAR16    *Name;
  BOOLEAN         Passed;
  UINT64          Value;
  BOOLEAN         HasValue;
} RESULT;

STATIC RESULT    mResults[MAX_RESULTS];
STATIC UINTN     mResultCount;
STATIC UINTN     mClicks;
STATIC lv_obj_t  *mCounterLabel;

/**
  Record a result; printed after shutdown.
**/
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

/**
  Record a result together with the measured value.
**/
STATIC
VOID
CheckValue (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed,
  IN UINT64        Value
  )
{
  Check (Name, Passed);
  if (mResultCount > 0) {
    mResults[mResultCount - 1].Value    = Value;
    mResults[mResultCount - 1].HasValue = TRUE;
  }
}

STATIC
VOID
ButtonClicked (
  lv_event_t  *e
  )
{
  mClicks++;
  lv_label_set_text_fmt (mCounterLabel, "Clicks: %u", (unsigned)mClicks);
}

STATIC
VOID
ExitTimer (
  lv_timer_t  *t
  )
{
  UefiGuiRequestExit ();
}

/**
  Read one pixel through GOP.
**/
STATIC
UINT32
ReadPixel (
  IN UINTN  X,
  IN UINTN  Y
  )
{
  UINT32  Value;

  if (EFI_ERROR (UefiGuiDisplayReadRect (&Value, X, Y, 1, 1))) {
    return 0xFFFFFFFF;
  }

  return Value;
}

/**
  Create the test screen.

  @return  The probe object.
**/
STATIC
lv_obj_t *
BuildScreen (
  IN UINT32  Width,
  IN UINT32  Height
  )
{
  lv_obj_t  *Screen;
  lv_obj_t  *Label;
  lv_obj_t  *Button;
  lv_obj_t  *Slider;
  lv_obj_t  *Spinner;
  lv_obj_t  *Probe;

  Screen = lv_screen_active ();
  lv_obj_set_style_bg_color (Screen, lv_color_hex (SCREEN_COLOR), 0);
  lv_obj_set_style_bg_opa (Screen, LV_OPA_COVER, 0);

  Label = lv_label_create (Screen);
  lv_label_set_text (Label, "LVGLUefiGuiPkg");
  lv_obj_set_style_text_font (Label, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color (Label, lv_color_hex (0xFFFFFF), 0);
  lv_obj_align (Label, LV_ALIGN_TOP_MID, 0, 40);

  Label = lv_label_create (Screen);
  lv_label_set_text_fmt (
    Label,
    "LVGL %d.%d.%d on UEFI GOP, %ux%u, partial rendering",
    LVGL_VERSION_MAJOR,
    LVGL_VERSION_MINOR,
    LVGL_VERSION_PATCH,
    (unsigned)Width,
    (unsigned)Height
    );
  lv_obj_set_style_text_font (Label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color (Label, lv_color_hex (0xB0BEC5), 0);
  lv_obj_align (Label, LV_ALIGN_TOP_MID, 0, 90);

  Button = lv_button_create (Screen);
  lv_obj_set_size (Button, 220, 64);
  lv_obj_align (Button, LV_ALIGN_CENTER, 0, -40);
  lv_obj_add_event_cb (Button, ButtonClicked, LV_EVENT_CLICKED, NULL);
  Label = lv_label_create (Button);
  lv_label_set_text (Label, "Click me");
  lv_obj_set_style_text_font (Label, &lv_font_montserrat_20, 0);
  lv_obj_center (Label);

  mCounterLabel = lv_label_create (Screen);
  lv_label_set_text (mCounterLabel, "Clicks: 0");
  lv_obj_set_style_text_font (mCounterLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color (mCounterLabel, lv_color_hex (0xFFFFFF), 0);
  lv_obj_align (mCounterLabel, LV_ALIGN_CENTER, 0, 30);

  Slider = lv_slider_create (Screen);
  lv_obj_set_width (Slider, 300);
  lv_slider_set_value (Slider, 65, LV_ANIM_OFF);
  lv_obj_align (Slider, LV_ALIGN_CENTER, 0, 90);

  Spinner = lv_spinner_create (Screen);
  lv_obj_set_size (Spinner, 64, 64);
  lv_obj_align (Spinner, LV_ALIGN_BOTTOM_RIGHT, -40, -40);

  Label = lv_label_create (Screen);
  lv_label_set_text (Label, "unscii-16: The quick brown fox 0123456789");
  lv_obj_set_style_text_font (Label, &lv_font_unscii_16, 0);
  lv_obj_set_style_text_color (Label, lv_color_hex (0x80CBC4), 0);
  lv_obj_align (Label, LV_ALIGN_BOTTOM_MID, 0, -120);

  //
  // Color probe: a plain rectangle in an asymmetric color, so swapped
  // channels or a wrong byte order are detected by read-back.
  //
  Probe = lv_obj_create (Screen);
  lv_obj_remove_style_all (Probe);
  lv_obj_set_size (Probe, PROBE_SIZE, PROBE_SIZE);
  lv_obj_set_pos (Probe, 20, (int32_t)Height - 20 - PROBE_SIZE);
  lv_obj_set_style_bg_color (Probe, lv_color_hex (PROBE_COLOR), 0);
  lv_obj_set_style_bg_opa (Probe, LV_OPA_COVER, 0);
  return Probe;
}

/**
  Configuration validation and calls before initialization.
**/
STATIC
VOID
TestConfig (
  VOID
  )
{
  UEFI_GUI_CONFIG        Config;
  UEFI_GUI_DISPLAY_INFO  Info;
  UEFI_GUI_RENDER_STATS  Stats;

  UefiGuiConfigInitDefaults (&Config);
  Check (
    L"defaults: revision, 64 lines, wait 1..50 ms",
    (BOOLEAN)((Config.Revision == UEFI_GUI_CONFIG_REVISION) && (Config.Display.DrawBufferLines == 64) &&
              (Config.Timer.MinWaitMs == 1) && (Config.Timer.MaxWaitMs == 50))
    );

  Config.Revision = 0;
  Check (L"wrong revision rejected", (BOOLEAN)(UefiGuiInitialize (&Config) == EFI_INCOMPATIBLE_VERSION));
  UefiGuiConfigInitDefaults (&Config);
  Config.Display.DrawBufferLines = 0;
  Check (L"0 draw lines rejected", (BOOLEAN)(UefiGuiInitialize (&Config) == EFI_INVALID_PARAMETER));
  UefiGuiConfigInitDefaults (&Config);
  Config.Timer.MinWaitMs = 20;
  Config.Timer.MaxWaitMs = 10;
  Check (L"MaxWait < MinWait rejected", (BOOLEAN)(UefiGuiInitialize (&Config) == EFI_INVALID_PARAMETER));
  UefiGuiConfigInitDefaults (&Config);
  Config.Display.PreferredWidth = 800;
  Check (L"width without height rejected", (BOOLEAN)(UefiGuiInitialize (&Config) == EFI_INVALID_PARAMETER));

  Check (L"GetDisplayInfo before init -> NOT_STARTED", (BOOLEAN)(UefiGuiGetDisplayInfo (&Info) == EFI_NOT_STARTED));
  Check (L"ProcessEvents before init -> NOT_STARTED", (BOOLEAN)(UefiGuiProcessEvents () == EFI_NOT_STARTED));
  Check (L"Run before init -> NOT_STARTED", (BOOLEAN)(UefiGuiRun () == EFI_NOT_STARTED));
  Check (L"GetRenderStats before init -> NOT_STARTED", (BOOLEAN)(UefiGuiGetRenderStats (&Stats) == EFI_NOT_STARTED));
  UefiGuiShutdown ();
  Check (L"Shutdown before init is harmless", TRUE);
}

/**
  Time base against gBS->Stall().
**/
STATIC
VOID
TestTimeBase (
  VOID
  )
{
  UINT64  Start;
  UINT64  Elapsed;

  Check (L"time base calibrated", (BOOLEAN)(UefiGuiTimeInitialize () == EFI_SUCCESS));
  CheckValue (L"TSC frequency (Hz)", (BOOLEAN)(UefiGuiGetTimeFrequency () > 1000000), UefiGuiGetTimeFrequency ());
  Start = UefiGuiGetTimeUs ();
  gBS->Stall (100000);
  Elapsed = UefiGuiGetTimeUs () - Start;
  CheckValue (L"Stall(100 ms) measures 95..130 ms (us)", (BOOLEAN)((Elapsed >= 95000) && (Elapsed <= 130000)), Elapsed);
}

/**
  Main GUI session.
**/
STATIC
VOID
TestGuiSession (
  VOID
  )
{
  UEFI_GUI_CONFIG        Config;
  UEFI_GUI_DISPLAY_INFO  Info;
  UEFI_GUI_RENDER_STATS  Before;
  UEFI_GUI_RENDER_STATS  After;
  EFI_STATUS             LastError;
  lv_obj_t               *Probe;
  UINT64                 ScreenPixels;
  UINT64                 Bands;
  UINT64                 Start;
  UINT64                 Elapsed;
  UINTN                  ProbeX;
  UINTN                  ProbeY;

  UefiGuiConfigInitDefaults (&Config);
  Config.Display.DrawBufferLines = DRAW_LINES;
  Check (L"UefiGuiInitialize", (BOOLEAN)(UefiGuiInitialize (&Config) == EFI_SUCCESS));
  Check (L"second Initialize -> ALREADY_STARTED", (BOOLEAN)(UefiGuiInitialize (&Config) == EFI_ALREADY_STARTED));
  if (EFI_ERROR (UefiGuiGetDisplayInfo (&Info))) {
    Check (L"UefiGuiGetDisplayInfo", FALSE);
    return;
  }

  Check (L"LVGL display matches GOP mode", (BOOLEAN)(
                                                      (lv_display_get_horizontal_resolution (lv_display_get_default ()) == (int32_t)Info.HorizontalResolution) &&
                                                      (lv_display_get_vertical_resolution (lv_display_get_default ()) == (int32_t)Info.VerticalResolution)));

  ScreenPixels = (UINT64)Info.HorizontalResolution * Info.VerticalResolution;
  Bands        = (Info.VerticalResolution + DRAW_LINES - 1) / DRAW_LINES;
  Probe        = BuildScreen (Info.HorizontalResolution, Info.VerticalResolution);
  ProbeX       = 20 + PROBE_SIZE / 2;
  ProbeY       = Info.VerticalResolution - 20 - PROBE_SIZE / 2;

  //
  // Full refresh: split into bands of DRAW_LINES lines.
  //
  UefiGuiGetRenderStats (&Before);
  lv_obj_invalidate (lv_screen_active ());
  lv_refr_now (NULL);
  UefiGuiGetRenderStats (&After);
  CheckValue (L"full refresh: one frame", (BOOLEAN)(After.FrameCount - Before.FrameCount == 1), After.FrameCount - Before.FrameCount);
  CheckValue (L"full refresh: one flush per band", (BOOLEAN)(After.FlushCount - Before.FlushCount == Bands), After.FlushCount - Before.FlushCount);
  CheckValue (L"full refresh: flushed pixels == screen", (BOOLEAN)(After.FlushedPixels - Before.FlushedPixels == ScreenPixels), After.FlushedPixels - Before.FlushedPixels);
  Check (L"full refresh: no flush errors", (BOOLEAN)(After.FlushErrors == Before.FlushErrors));

  CheckValue (L"probe color read back through GOP (0xE04010)", (BOOLEAN)(ReadPixel (ProbeX, ProbeY) == PROBE_COLOR), ReadPixel (ProbeX, ProbeY));
  Check (L"probe edges exact", (BOOLEAN)((ReadPixel (20, ProbeY) == PROBE_COLOR) && (ReadPixel (19, ProbeY) == SCREEN_COLOR) &&
                                         (ReadPixel (20 + PROBE_SIZE - 1, ProbeY) == PROBE_COLOR) && (ReadPixel (20 + PROBE_SIZE, ProbeY) == SCREEN_COLOR)));
  CheckValue (L"screen background read back (0x1E2530)", (BOOLEAN)(ReadPixel (Info.HorizontalResolution - 3, 3) == SCREEN_COLOR), ReadPixel (Info.HorizontalResolution - 3, 3));
  Check (L"probe object is where LVGL placed it", (BOOLEAN)((lv_obj_get_x (Probe) == 20) && (lv_obj_get_width (Probe) == PROBE_SIZE)));

  //
  // Small change: partial rendering flushes a small area only.
  //
  UefiGuiGetRenderStats (&Before);
  lv_obj_send_event (lv_obj_get_child (lv_screen_active (), 2), LV_EVENT_CLICKED, NULL);
  Check (L"Button LV_EVENT_CLICKED handler ran", (BOOLEAN)(mClicks == 1));
  lv_refr_now (NULL);
  UefiGuiGetRenderStats (&After);
  CheckValue (
    L"label change flushes < 5% of the screen (pixels)",
    (BOOLEAN)((After.FlushedPixels - Before.FlushedPixels > 0) && ((After.FlushedPixels - Before.FlushedPixels) * 20 < ScreenPixels)),
    After.FlushedPixels - Before.FlushedPixels
    );

  //
  // Flush failures: LVGL must keep going and the error must be reported.
  //
  UefiGuiGetRenderStats (&Before);
  UefiGuiDebugInjectFlushFailures (3);
  lv_obj_invalidate (lv_screen_active ());
  lv_refr_now (NULL);
  UefiGuiGetRenderStats (&After);
  CheckValue (L"3 injected flush failures counted", (BOOLEAN)(After.FlushErrors - Before.FlushErrors == 3), After.FlushErrors - Before.FlushErrors);
  Check (L"refresh completed despite failures (frame counted)", (BOOLEAN)(After.FrameCount - Before.FrameCount == 1));
  Check (L"GetLastError reports EFI_DEVICE_ERROR", (BOOLEAN)((UefiGuiGetLastError (&LastError) == EFI_SUCCESS) && (LastError == EFI_DEVICE_ERROR)));
  UefiGuiGetRenderStats (&Before);
  lv_obj_invalidate (lv_screen_active ());
  lv_refr_now (NULL);
  UefiGuiGetRenderStats (&After);
  Check (L"next refresh succeeds", (BOOLEAN)((After.FlushErrors == Before.FlushErrors) && (After.FlushedPixels - Before.FlushedPixels == ScreenPixels)));
  Check (L"probe correct after recovery", (BOOLEAN)(ReadPixel (ProbeX, ProbeY) == PROBE_COLOR));

  //
  // Event loop: the spinner animates, an LVGL timer requests the exit.
  //
  UefiGuiGetRenderStats (&Before);
  lv_timer_set_repeat_count (lv_timer_create (ExitTimer, RUN_MS, NULL), 1);
  Start = UefiGuiGetTimeMs ();
  Check (L"UefiGuiRun returns after RequestExit", (BOOLEAN)(UefiGuiRun () == EFI_SUCCESS));
  Elapsed = UefiGuiGetTimeMs () - Start;
  UefiGuiGetRenderStats (&After);
  CheckValue (L"run time ~6 s (ms)", (BOOLEAN)((Elapsed >= RUN_MS) && (Elapsed < RUN_MS + 1000)), Elapsed);
  CheckValue (L"frames rendered while running (spinner)", (BOOLEAN)(After.FrameCount - Before.FrameCount >= 30), After.FrameCount - Before.FrameCount);
  CheckValue (L"loop iterations <= elapsed ms (no busy loop)", (BOOLEAN)(After.LoopIterations - Before.LoopIterations <= Elapsed), After.LoopIterations - Before.LoopIterations);
  CheckValue (
    L"loop slept > 50% of the time (us)",
    (BOOLEAN)((After.WaitTimeUs - Before.WaitTimeUs) * 2 > Elapsed * 1000),
    After.WaitTimeUs - Before.WaitTimeUs
    );
  CheckValue (L"no flush errors while running", (BOOLEAN)(After.FlushErrors == Before.FlushErrors), After.FlushErrors - Before.FlushErrors);
  CheckValue (L"flush time per frame (us, info)", TRUE, (After.FrameCount > Before.FrameCount) ? DivU64x64Remainder (After.FlushTimeUs - Before.FlushTimeUs, After.FrameCount - Before.FrameCount, NULL) : 0);

  UefiGuiShutdown ();
  Check (L"Shutdown leaves UefiGui uninitialized", (BOOLEAN)(UefiGuiGetDisplayInfo (&Info) == EFI_NOT_STARTED));
}

/**
  Second, short session: LVGL can be initialized again after lv_deinit().
**/
STATIC
VOID
TestReinit (
  VOID
  )
{
  lv_obj_t  *Label;

  Check (L"re-initialize with defaults", (BOOLEAN)(UefiGuiInitialize (NULL) == EFI_SUCCESS));
  Label = lv_label_create (lv_screen_active ());
  lv_label_set_text (Label, "second session");
  lv_refr_now (NULL);
  Check (L"second session renders", (BOOLEAN)(lv_obj_get_width (Label) > 0));
  UefiGuiShutdown ();
}

/**
  Entry point.
**/
EFI_STATUS
EFIAPI
LvglPortTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UEFI_GUI_MEMORY_STATS  MemBefore;
  UEFI_GUI_MEMORY_STATS  MemPeak;
  UEFI_GUI_MEMORY_STATS  MemAfter;
  UINTN                  Index;
  UINTN                  Failures;

  Print (L"LvglPortTest - LVGLUefiGuiPkg Phase 2\n");
  UefiGuiGetMemoryStats (&MemBefore);

  TestConfig ();
  TestTimeBase ();

  //
  // From here until shutdown the GUI owns the screen; nothing is printed.
  //
  Print (L"LVGLPORTTEST: SCREEN READY\n");
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  TestGuiSession ();
  TestReinit ();
  gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  gST->ConOut->ClearScreen (gST->ConOut);

  UefiGuiGetMemoryStats (&MemPeak);
  UefiGuiGetMemoryStats (&MemAfter);
  CheckValue (L"peak UefiGui memory (bytes, info)", TRUE, MemPeak.PeakBytes);
  CheckValue (L"all memory released after shutdown (bytes left)", (BOOLEAN)(MemAfter.CurrentBytes == MemBefore.CurrentBytes), MemAfter.CurrentBytes - MemBefore.CurrentBytes);
  Check (L"no invalid frees", (BOOLEAN)(MemAfter.InvalidFreeCount == MemBefore.InvalidFreeCount));
  Check (L"no failed allocations", (BOOLEAN)(MemAfter.FailureCount == MemBefore.FailureCount));

  Failures = 0;
  for (Index = 0; Index < mResultCount; Index++) {
    if (!mResults[Index].Passed) {
      Failures++;
    }

    if (mResults[Index].HasValue) {
      Print (L"  [%s] %s: %lu\n", mResults[Index].Passed ? L"PASS" : L"FAIL", mResults[Index].Name, mResults[Index].Value);
    } else {
      Print (L"  [%s] %s\n", mResults[Index].Passed ? L"PASS" : L"FAIL", mResults[Index].Name);
    }
  }

  Print (L"%lu checks, %lu failed\n", (UINT64)mResultCount, (UINT64)Failures);
  Print (L"UEFIGUI-LVGLPORT: %s\n", (Failures == 0) ? L"PASS" : L"FAIL");
  return (Failures == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
