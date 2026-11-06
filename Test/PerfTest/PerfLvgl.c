/** @file
  PerfTest sections 2, 4 and 6: LVGL full and partial update times, memory
  consumption, behavior at different GOP resolutions, summary screen.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "PerfTest.h"

STATIC PERF_SAMPLES  mTotal;
STATIC PERF_SAMPLES  mFlush;
STATIC PERF_SAMPLES  mRender;
STATIC PERF_SAMPLES  mPixels;

/**
  Run Reps full-screen refreshes and record total, flush and render times.

  @return  Number of refreshes that did not flush exactly the whole screen.
**/
STATIC
UINTN
MeasureFull (
  IN CONST CHAR8  *Prefix,
  IN UINTN        Reps,
  IN UINT64       ScreenPixels,
  OUT UINT64      *MedianTotal OPTIONAL
  )
{
  UINTN         Index;
  UINTN         Wrong;
  UINT64        Total;
  UINT64        Flush;
  UINT64        Pixels;
  CHAR8         Key[64];
  PERF_SUMMARY  Summary;

  mTotal.Count  = 0;
  mFlush.Count  = 0;
  mRender.Count = 0;
  Wrong         = 0;
  for (Index = 0; Index < Reps; Index++) {
    RefreshMeasured (TRUE, &Total, &Flush, &Pixels);
    SampleAdd (&mTotal, Total);
    SampleAdd (&mFlush, Flush);
    SampleAdd (&mRender, (Total > Flush) ? Total - Flush : 0);
    if (Pixels != ScreenPixels) {
      Wrong++;
    }
  }

  AsciiSPrint (Key, sizeof (Key), "%a.total", Prefix);
  MetricSummary (Key, &mTotal, &Summary);
  if (MedianTotal != NULL) {
    *MedianTotal = Summary.Median;
  }

  AsciiSPrint (Key, sizeof (Key), "%a.flush", Prefix);
  MetricSummary (Key, &mFlush, NULL);
  AsciiSPrint (Key, sizeof (Key), "%a.render", Prefix);
  MetricSummary (Key, &mRender, NULL);
  return Wrong;
}

typedef enum {
  PartialLabel,
  PartialTextArea,
  PartialBar
} PARTIAL_KIND;

/**
  Change one widget and refresh, Reps times.

  @return  Largest number of pixels flushed by one refresh.
**/
STATIC
UINT64
MeasurePartial (
  IN PERF_SCENE    *Scene,
  IN PARTIAL_KIND  Kind,
  IN CONST CHAR8   *Prefix,
  IN UINTN         Reps
  )
{
  UINTN         Index;
  UINT64        Total;
  UINT64        Flush;
  UINT64        Pixels;
  UINT64        MaxPixels;
  CHAR8         Key[64];
  PERF_SUMMARY  Summary;

  mTotal.Count  = 0;
  mFlush.Count  = 0;
  mPixels.Count = 0;
  MaxPixels     = 0;
  lv_refr_now (NULL);
  for (Index = 0; Index < Reps; Index++) {
    switch (Kind) {
      case PartialLabel:
        lv_label_set_text_fmt (Scene->ButtonLabel[1], "Clicked %u", (UINT32)Index);
        break;
      case PartialTextArea:
        if ((Index % 40) == 39) {
          lv_textarea_set_text (Scene->TextArea, "");
          lv_refr_now (NULL);
        }

        lv_textarea_add_char (Scene->TextArea, (UINT32)('a' + Index % 26));
        break;
      default:
        lv_bar_set_value (Scene->Bar, (INT32)((Index * 7) % 100), LV_ANIM_OFF);
        break;
    }

    RefreshMeasured (FALSE, &Total, &Flush, &Pixels);
    SampleAdd (&mTotal, Total);
    SampleAdd (&mFlush, Flush);
    SampleAdd (&mPixels, Pixels);
    MaxPixels = MAX (MaxPixels, Pixels);
  }

  AsciiSPrint (Key, sizeof (Key), "%a.total", Prefix);
  MetricSummary (Key, &mTotal, NULL);
  AsciiSPrint (Key, sizeof (Key), "%a.flush", Prefix);
  MetricSummary (Key, &mFlush, NULL);
  Summarize (&mPixels, &Summary);
  MetricF (Summary.Median, "px", "%a.pixels.median", Prefix);
  MetricF (Summary.Max, "px", "%a.pixels.max", Prefix);
  return MaxPixels;
}

/**
  Initialize, build the scene and measure full-screen updates for one
  display configuration. The default configuration also measures partial
  updates and memory.
**/
STATIC
VOID
MeasureConfiguration (
  IN UEFI_GUI_DISPLAY_BACKEND  Backend,
  IN UINT32                    DrawLines,
  IN CONST CHAR8               *Tag,
  IN BOOLEAN                   Detailed,
  IN UINT64                    Baseline
  )
{
  EFI_STATUS             Status;
  UEFI_GUI_DISPLAY_INFO  Info;
  UEFI_GUI_MEMORY_STATS  MemStats;
  PERF_SCENE             Scene;
  UINT64                 ScreenPixels;
  UINT64                 AfterInit;
  UINT64                 AfterScene;
  UINT64                 FreeBefore;
  UINT64                 FreeAfter;
  UINT64                 Bytes;
  UINT64                 MaxPixels;
  UINTN                  Wrong;
  UINTN                  Index;
  lv_obj_t               *Button;
  CHAR8                  Prefix[64];

  FreeBefore = FreeSystemKib ();
  Status     = InitGui (Backend, DrawLines, 0, 0);
  if (Backend == UefiGuiDisplayBackendFrameBuffer) {
    if (Status == EFI_UNSUPPORTED) {
      Check (L"2 FrameBuffer backend refused cleanly (EFI_UNSUPPORTED)", (BOOLEAN)(GuiBytes () == Baseline));
      return;
    }
  }

  if (EFI_ERROR (Status)) {
    CheckValue (L"2 UefiGuiInitialize for an update configuration", FALSE, (INT64)Status);
    return;
  }

  UefiGuiGetDisplayInfo (&Info);
  ScreenPixels = (UINT64)Info.HorizontalResolution * Info.VerticalResolution;
  AfterInit    = GuiBytes ();
  BuildScene (&Scene);
  Pump (300);
  AfterScene = GuiBytes ();

  AsciiSPrint (Prefix, sizeof (Prefix), "lvgl.full.%a", Tag);
  Wrong = MeasureFull (Prefix, 12, ScreenPixels, NULL);
  if (Detailed) {
    CheckValue (L"2 default config: every full refresh flushed exactly the whole screen", (BOOLEAN)(Wrong == 0), (INT64)Wrong);
  } else {
    CheckValue (L"2 other configs: every full refresh flushed exactly the whole screen", (BOOLEAN)(Wrong == 0), (INT64)Wrong);
  }

  MetricF (AfterInit - Baseline, "B", "memory.gui.%a.after_init", Tag);
  MetricF (AfterScene - Baseline, "B", "memory.gui.%a.with_scene", Tag);

  if (Detailed) {
    MaxPixels = MeasurePartial (&Scene, PartialLabel, "lvgl.partial.button_label", 30);
    CheckValue (L"2 partial update (button label) flushes less than 2% of the screen", (BOOLEAN)(MaxPixels * 50 < ScreenPixels), (INT64)MaxPixels);
    MaxPixels = MeasurePartial (&Scene, PartialTextArea, "lvgl.partial.textarea_char", 30);
    CheckValue (L"2 partial update (text area character) flushes less than 10% of the screen", (BOOLEAN)(MaxPixels * 10 < ScreenPixels), (INT64)MaxPixels);
    MaxPixels = MeasurePartial (&Scene, PartialBar, "lvgl.partial.bar_value", 30);
    CheckValue (L"2 partial update (bar) flushes less than 2% of the screen", (BOOLEAN)(MaxPixels * 50 < ScreenPixels), (INT64)MaxPixels);

    //
    // Memory per widget: 100 buttons with a label each.
    //
    // Warm-up cycle first: LVGL keeps some lazily grown internal state.
    for (Index = 0; Index < 100; Index++) {
      lv_label_set_text_fmt (lv_label_create (lv_button_create (Scene.Churn)), "B%u", (UINT32)Index);
    }

    lv_refr_now (NULL);
    lv_obj_clean (Scene.Churn);
    lv_refr_now (NULL);
    Bytes = GuiBytes ();
    for (Index = 0; Index < 100; Index++) {
      Button = lv_button_create (Scene.Churn);
      lv_label_set_text_fmt (lv_label_create (Button), "B%u", (UINT32)Index);
    }

    lv_refr_now (NULL);
    Metric ("memory.gui.per_button_with_label", DivU64x64Remainder (GuiBytes () - Bytes, 100, NULL), "B");
    lv_obj_clean (Scene.Churn);
    lv_refr_now (NULL);
    CheckValue (L"2 memory returns to its value after deleting 100 buttons", (BOOLEAN)(GuiBytes () == Bytes), (INT64)(GuiBytes () - Bytes));

    UefiGuiGetMemoryStats (&MemStats);
    Metric ("memory.gui.allocations_live", MemStats.AllocationCount - MemStats.FreeCount, "n");
    Metric ("memory.draw_buffer", (UINT64)Info.HorizontalResolution * MIN (64, Info.VerticalResolution) * 4, "B");
    Metric ("memory.blt_conversion_buffer", (UINT64)Info.HorizontalResolution * MIN (64, Info.VerticalResolution) * 4, "B");
    FreeAfter = FreeSystemKib ();
    Metric ("memory.system.used_by_session", (FreeBefore >= FreeAfter) ? FreeBefore - FreeAfter : 0, "KiB");
  }

  UefiGuiShutdown ();
  if (Detailed) {
    CheckValue (L"2 default config: memory back to baseline after shutdown", (BOOLEAN)(GuiBytes () == Baseline), (INT64)(GuiBytes () - Baseline));
  } else {
    CheckValue (L"2 other configs: memory back to baseline after shutdown", (BOOLEAN)(GuiBytes () == Baseline), (INT64)(GuiBytes () - Baseline));
  }
}

VOID
SectionUpdatesAndMemory (
  VOID
  )
{
  UINT64                 Baseline;
  UEFI_GUI_MEMORY_STATS  MemStats;

  Baseline = GuiBytes ();
  MeasureConfiguration (UefiGuiDisplayBackendAuto, 64, "blt.lines64", TRUE, Baseline);
  UefiGuiGetMemoryStats (&MemStats);
  Metric ("memory.gui.peak_default_session", MemStats.PeakBytes - Baseline, "B");
  MeasureConfiguration (UefiGuiDisplayBackendBlt, 16, "blt.lines16", FALSE, Baseline);
  MeasureConfiguration (UefiGuiDisplayBackendBlt, 4096, "blt.fullheight", FALSE, Baseline);
  MeasureConfiguration (UefiGuiDisplayBackendFrameBuffer, 64, "framebuffer.lines64", FALSE, Baseline);
}

// ---------------------------------------------------------------------------
// Section 4: resolutions.
// ---------------------------------------------------------------------------

typedef struct {
  UINT32    Width;
  UINT32    Height;
} RESOLUTION;

STATIC CONST RESOLUTION  mResolutions[] = {
  { 640,  480  },
  { 800,  600  },
  { 1024, 768  },
  { 1280, 1024 },
  { 1920, 1080 },
  { 2560, 1600 },
};

STATIC
BOOLEAN
ModeExists (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                        Width,
  IN UINT32                        Height
  )
{
  UINT32                                Mode;
  UINTN                                 Size;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  BOOLEAN                               Found;

  Found = FALSE;
  for (Mode = 0; Mode < Gop->Mode->MaxMode && !Found; Mode++) {
    if (!EFI_ERROR (Gop->QueryMode (Gop, Mode, &Size, &Info))) {
      Found = (BOOLEAN)((Info->HorizontalResolution == Width) && (Info->VerticalResolution == Height));
      FreePool (Info);
    }
  }

  return Found;
}

VOID
SectionResolutions (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Blt;
  UEFI_GUI_DISPLAY_INFO          Info;
  PERF_SCENE                     Scene;
  PERF_SAMPLES                   *Raw;
  PERF_SUMMARY                   Summary;
  UINT32                         OrigMode;
  UINT64                         Baseline;
  UINT64                         Median;
  UINT64                         Start;
  UINT32                         Pixel;
  UINTN                          Index;
  UINTN                          Rep;
  UINTN                          Tested;
  UINTN                          InfoWrong;
  UINTN                          PixelWrong;
  UINTN                          FlushWrong;
  UINTN                          ModeWrong;
  UINTN                          MemWrong;
  UINTN                          InitFailed;
  UINT32                         W;
  UINT32                         H;
  CHAR8                          Prefix[64];

  Gop      = FindGop ();
  OrigMode = Gop->Mode->Mode;
  Baseline = GuiBytes ();
  Raw      = &mPixels;
  Tested   = 0;
  InfoWrong  = 0;
  PixelWrong = 0;
  FlushWrong = 0;
  ModeWrong  = 0;
  MemWrong   = 0;
  InitFailed = 0;

  for (Index = 0; Index < ARRAY_SIZE (mResolutions); Index++) {
    W = mResolutions[Index].Width;
    H = mResolutions[Index].Height;
    if (!ModeExists (Gop, W, H)) {
      MetricF (0, "bool", "res.%ux%u.available", W, H);
      continue;
    }

    MetricF (1, "bool", "res.%ux%u.available", W, H);
    if (EFI_ERROR (InitGui (UefiGuiDisplayBackendAuto, 0, W, H))) {
      InitFailed++;
      continue;
    }

    Tested++;
    UefiGuiGetDisplayInfo (&Info);
    if ((Info.HorizontalResolution != W) || (Info.VerticalResolution != H) ||
        (Info.PixelsPerScanLine < W) || (Gop->Mode->Info->HorizontalResolution != W))
    {
      InfoWrong++;
    }

    MetricF (Info.PixelsPerScanLine, "px", "res.%ux%u.pixels_per_scan_line", W, H);
    MetricF (GuiBytes () - Baseline, "B", "res.%ux%u.memory_after_init", W, H);
    BuildScene (&Scene);
    Pump (300);
    MetricF (GuiBytes () - Baseline, "B", "res.%ux%u.memory_with_scene", W, H);

    AsciiSPrint (Prefix, sizeof (Prefix), "res.%ux%u.full", W, H);
    FlushWrong += MeasureFull (Prefix, 6, (UINT64)W * H, &Median);
    if (Median != 0) {
      MetricF (DivU64x64Remainder (1000000, Median, NULL), "fps", "res.%ux%u.full_refresh_rate", W, H);
    }

    AsciiSPrint (Prefix, sizeof (Prefix), "res.%ux%u.partial_label", W, H);
    MeasurePartial (&Scene, PartialLabel, Prefix, 12);

    //
    // Raw GOP full-screen Blt at this mode.
    //
    Blt = AllocateZeroPool ((UINTN)W * H * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    if (Blt != NULL) {
      Raw->Count = 0;
      for (Rep = 0; Rep < 6; Rep++) {
        Start = UefiGuiGetTimeUs ();
        Gop->Blt (Gop, Blt, EfiBltBufferToVideo, 0, 0, 0, 0, W, H, 0);
        SampleAdd (Raw, UefiGuiGetTimeUs () - Start);
      }

      Summarize (Raw, &Summary);
      MetricF (Summary.Median, "us", "res.%ux%u.gop_full_blt.median", W, H);
      FreePool (Blt);
    }

    //
    // Redraw and check a background pixel below the root container.
    //
    lv_obj_invalidate (lv_screen_active ());
    lv_refr_now (NULL);
    if (EFI_ERROR (UefiGuiDisplayReadRect (&Pixel, W / 2, H - 3, 1, 1)) || ((Pixel & 0xFFFFFF) != PERF_SCENE_BG)) {
      PixelWrong++;
    }

    UefiGuiShutdown ();
    if (Gop->Mode->Mode != OrigMode) {
      ModeWrong++;
    }

    if (GuiBytes () != Baseline) {
      MemWrong++;
    }
  }

  Metric ("res.modes_tested", Tested, "n");
  CheckValue (L"4 at least three GOP resolutions tested", (BOOLEAN)(Tested >= 3), (INT64)Tested);
  CheckValue (L"4 every listed mode the GOP offers initialized", (BOOLEAN)(InitFailed == 0), (INT64)InitFailed);
  CheckValue (L"4 display information matches the selected mode", (BOOLEAN)(InfoWrong == 0), (INT64)InfoWrong);
  CheckValue (L"4 every full refresh flushed exactly the whole screen", (BOOLEAN)(FlushWrong == 0), (INT64)FlushWrong);
  CheckValue (L"4 background pixel read back correctly at every mode", (BOOLEAN)(PixelWrong == 0), (INT64)PixelWrong);
  CheckValue (L"4 original GOP mode restored after every session", (BOOLEAN)(ModeWrong == 0), (INT64)ModeWrong);
  CheckValue (L"4 memory back to baseline after every session", (BOOLEAN)(MemWrong == 0), (INT64)MemWrong);
}

// ---------------------------------------------------------------------------
// Section 6: summary screen.
// ---------------------------------------------------------------------------

STATIC
VOID
AddRow (
  IN lv_obj_t     *Table,
  IN UINT32       *Row,
  IN CONST CHAR8  *Label,
  IN CONST CHAR8  *Key,
  IN BOOLEAN      AsKiB
  )
{
  UINT64  Value;

  if (!MetricGet (Key, &Value)) {
    return;
  }

  lv_table_set_cell_value (Table, *Row, 0, Label);
  if (AsKiB) {
    lv_table_set_cell_value_fmt (Table, *Row, 1, "%u KiB", (UINT32)(Value / 1024));
  } else {
    lv_table_set_cell_value_fmt (Table, *Row, 1, "%u.%02u ms", (UINT32)(Value / 1000), (UINT32)((Value % 1000) / 10));
  }

  (*Row)++;
}

VOID
SectionSummaryScreen (
  VOID
  )
{
  lv_obj_t  *Screen;
  lv_obj_t  *Title;
  lv_obj_t  *Table;
  UINT32    Row;
  UINT64    Value;

  if (EFI_ERROR (InitGui (UefiGuiDisplayBackendAuto, 0, 0, 0))) {
    Check (L"6 summary screen initialized", FALSE);
    return;
  }

  Screen = lv_screen_active ();
  lv_obj_set_style_bg_color (Screen, lv_color_hex (PERF_SCENE_BG), 0);
  Title = lv_label_create (Screen);
  lv_obj_set_style_text_font (Title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color (Title, lv_color_white (), 0);
  lv_label_set_text (Title, "PerfTest - LVGLUefiGuiPkg Phase 6 (medians, QEMU + OVMF)");
  lv_obj_align (Title, LV_ALIGN_TOP_MID, 0, 16);

  Table = lv_table_create (Screen);
  lv_table_set_column_count (Table, 2);
  lv_table_set_column_width (Table, 0, 560);
  lv_table_set_column_width (Table, 1, 220);
  lv_obj_set_height (Table, lv_pct (85));
  lv_obj_align (Table, LV_ALIGN_TOP_MID, 0, 64);

  Row = 0;
  AddRow (Table, &Row, "GOP Blt 64x64", "gop.buffer_to_video.64x64.median", FALSE);
  AddRow (Table, &Row, "GOP Blt full screen", "gop.buffer_to_video.full.median", FALSE);
  AddRow (Table, &Row, "Blt backend flush, full screen", "display.blt.xrgb8888.full.median", FALSE);
  AddRow (Table, &Row, "FrameBuffer backend flush, full screen", "display.framebuffer.xrgb8888.full.median", FALSE);
  AddRow (Table, &Row, "LVGL full-screen update (64-line buffer)", "lvgl.full.blt.lines64.total.median", FALSE);
  AddRow (Table, &Row, "LVGL partial update (button label)", "lvgl.partial.button_label.total.median", FALSE);
  AddRow (Table, &Row, "LVGL partial update (text area char)", "lvgl.partial.textarea_char.total.median", FALSE);
  AddRow (Table, &Row, "Key event to LVGL handler", "input.key.handler.median", FALSE);
  AddRow (Table, &Row, "Key event to refreshed screen", "input.key.pixels.median", FALSE);
  AddRow (Table, &Row, "Click to refreshed screen", "input.click.pixels.median", FALSE);
  AddRow (Table, &Row, "Memory after init", "memory.gui.blt.lines64.after_init", TRUE);
  AddRow (Table, &Row, "Memory with the test screen", "memory.gui.blt.lines64.with_scene", TRUE);
  AddRow (Table, &Row, "1920x1080 full-screen update", "res.1920x1080.full.total.median", FALSE);
  if (MetricGet ("soak.seconds", &Value)) {
    lv_table_set_cell_value (Table, Row, 0, "Soak duration");
    lv_table_set_cell_value_fmt (Table, Row, 1, "%u s", (UINT32)Value);
    Row++;
  }

  if (MetricGet ("soak.frames", &Value)) {
    lv_table_set_cell_value (Table, Row, 0, "Soak frames rendered");
    lv_table_set_cell_value_fmt (Table, Row, 1, "%u", (UINT32)Value);
    Row++;
  }

  if (MetricGet ("soak.memory_growth", &Value)) {
    lv_table_set_cell_value (Table, Row, 0, "Soak memory growth");
    lv_table_set_cell_value_fmt (Table, Row, 1, "%u B", (UINT32)Value);
    Row++;
  }

  Check (L"6 summary table has rows", (BOOLEAN)(Row >= 10));
  Pump (500);
  SendCmd ("screenshot");
  Pump (2500);
  UefiGuiShutdown ();
}
