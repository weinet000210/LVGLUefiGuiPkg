/** @file
  PerfTest - Phase 6 performance and stability measurements on the real
  firmware (QEMU + OVMF in CI).

  Sections:
    1. GOP: raw GOP Blt latency per rectangle size (BufferToVideo, VideoFill,
       VideoToBltBuffer), then the UefiGuiDisplayLib Blt backend and, when
       the frame buffer passes validation, the FrameBuffer backend on the
       same rectangles.
    2. LVGL updates and memory: full-screen and partial update times for
       several draw buffer sizes and both backends, split into rendering and
       flushing; memory after initialization, after building the screen, per
       widget, and after shutdown.
    3. Input latency: key and pointer events injected from a TPL_CALLBACK
       timer notification (the same wake path a firmware device event takes)
       to the LVGL event handler and to the first refresh showing the
       change; plus USB keyboard presses sent by the host for reference.
    4. Resolutions: every listed GOP mode the firmware offers, with the same
       screen: display information, update times, memory, GOP mode restore.
    5. Soak: animations, widget churn, injected typing and pointer moves and
       periodic full refreshes for a fixed time (default 60 s, "-soak N"
       on the command line), sampling memory and error counters.
    6. A summary screen (screenshot through the host driver).

  Every measurement is printed as "UEFIGUI-PERF <key> <value> <unit>";
  scripts/perf-report.py turns the console log into a table. Checks guard
  correctness (status codes, pixel read-back, memory returning to its
  baseline, no lost input, no errors) and only loose sanity bounds on
  times, because absolute times depend on the machine.

  The last console line is "UEFIGUI-PERFTEST: PASS" or "... FAIL".

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "PerfTest.h"
#include <Protocol/LoadedImage.h>

#define MAX_RESULTS   400
#define MAX_METRICS   600

typedef struct {
  CONST CHAR16    *Name;
  BOOLEAN         Passed;
  BOOLEAN         HasValue;
  INT64           Value;
} RESULT;

typedef struct {
  CHAR8     Key[64];
  UINT64    Value;
  CHAR8     Unit[12];
} METRIC;

STATIC RESULT                  mResults[MAX_RESULTS];
STATIC UINTN                   mResultCount;
STATIC METRIC                  mMetrics[MAX_METRICS];
STATIC UINTN                   mMetricCount;
STATIC EFI_SERIAL_IO_PROTOCOL  *mSerial;
STATIC UINTN                   mSeq;
STATIC UINT32                  mRandom = 0x2545F491;
STATIC UINT64                  mSortBuffer[PERF_MAX_SAMPLES];

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

VOID
Metric (
  IN CONST CHAR8  *Key,
  IN UINT64       Value,
  IN CONST CHAR8  *Unit
  )
{
  if (mMetricCount >= MAX_METRICS) {
    return;
  }

  AsciiStrCpyS (mMetrics[mMetricCount].Key, sizeof (mMetrics[0].Key), Key);
  AsciiStrCpyS (mMetrics[mMetricCount].Unit, sizeof (mMetrics[0].Unit), Unit);
  mMetrics[mMetricCount].Value = Value;
  mMetricCount++;
}

VOID
EFIAPI
MetricF (
  IN UINT64       Value,
  IN CONST CHAR8  *Unit,
  IN CONST CHAR8  *KeyFormat,
  ...
  )
{
  CHAR8    Key[64];
  VA_LIST  Marker;

  VA_START (Marker, KeyFormat);
  AsciiVSPrint (Key, sizeof (Key), KeyFormat, Marker);
  VA_END (Marker);
  Metric (Key, Value, Unit);
}

/**
  Look up a recorded metric.

  @retval TRUE  Found; *Value is set.
**/
BOOLEAN
MetricGet (
  IN  CONST CHAR8  *Key,
  OUT UINT64       *Value
  )
{
  UINTN  Index;

  for (Index = 0; Index < mMetricCount; Index++) {
    if (AsciiStrCmp (mMetrics[Index].Key, Key) == 0) {
      *Value = mMetrics[Index].Value;
      return TRUE;
    }
  }

  return FALSE;
}

VOID
SampleAdd (
  IN OUT PERF_SAMPLES  *Samples,
  IN UINT64            Value
  )
{
  if (Samples->Count < PERF_MAX_SAMPLES) {
    Samples->Value[Samples->Count++] = Value;
  }
}

VOID
Summarize (
  IN  CONST PERF_SAMPLES  *Samples,
  OUT PERF_SUMMARY        *Summary
  )
{
  UINTN   Index;
  UINTN   Inner;
  UINT64  Key;
  UINT64  Sum;

  ZeroMem (Summary, sizeof (*Summary));
  if (Samples->Count == 0) {
    return;
  }

  CopyMem (mSortBuffer, Samples->Value, Samples->Count * sizeof (UINT64));
  Sum = 0;
  for (Index = 0; Index < Samples->Count; Index++) {
    Key = mSortBuffer[Index];
    Sum += Key;
    Inner = Index;
    while ((Inner > 0) && (mSortBuffer[Inner - 1] > Key)) {
      mSortBuffer[Inner] = mSortBuffer[Inner - 1];
      Inner--;
    }

    mSortBuffer[Inner] = Key;
  }

  Summary->Min    = mSortBuffer[0];
  Summary->Max    = mSortBuffer[Samples->Count - 1];
  Summary->Median = mSortBuffer[Samples->Count / 2];
  Summary->P95    = mSortBuffer[(Samples->Count * 95) / 100 < Samples->Count ? (Samples->Count * 95) / 100 : Samples->Count - 1];
  Summary->Mean   = DivU64x64Remainder (Sum, Samples->Count, NULL);
}

VOID
MetricSummary (
  IN CONST CHAR8         *Prefix,
  IN CONST PERF_SAMPLES  *Samples,
  OUT PERF_SUMMARY       *Summary OPTIONAL
  )
{
  PERF_SUMMARY  Local;

  Summarize (Samples, &Local);
  MetricF (Samples->Count, "n", "%a.samples", Prefix);
  MetricF (Local.Min, "us", "%a.min", Prefix);
  MetricF (Local.Median, "us", "%a.median", Prefix);
  MetricF (Local.P95, "us", "%a.p95", Prefix);
  MetricF (Local.Max, "us", "%a.max", Prefix);
  MetricF (Local.Mean, "us", "%a.mean", Prefix);
  if (Summary != NULL) {
    *Summary = Local;
  }
}

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

UINT64
GuiBytes (
  VOID
  )
{
  UEFI_GUI_MEMORY_STATS  Stats;

  UefiGuiGetMemoryStats (&Stats);
  return Stats.CurrentBytes;
}

UINT64
FreeSystemKib (
  VOID
  )
{
  EFI_STATUS             Status;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINT64                 Pages;
  UINTN                  Offset;

  MapSize = 0;
  Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return 0;
  }

  MapSize += 16 * DescriptorSize;
  Map      = AllocatePool (MapSize);
  if (Map == NULL) {
    return 0;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);
  Pages  = 0;
  if (!EFI_ERROR (Status)) {
    for (Offset = 0; Offset < MapSize; Offset += DescriptorSize) {
      Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
      if (Desc->Type == EfiConventionalMemory) {
        Pages += Desc->NumberOfPages;
      }
    }
  }

  FreePool (Map);
  return Pages * 4;
}

EFI_GRAPHICS_OUTPUT_PROTOCOL *
FindGop (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  if (!EFI_ERROR (gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop))) {
    return Gop;
  }

  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop))) {
    return Gop;
  }

  return NULL;
}

EFI_STATUS
InitGui (
  IN UEFI_GUI_DISPLAY_BACKEND  Backend,
  IN UINT32                    DrawLines,
  IN UINT32                    Width,
  IN UINT32                    Height
  )
{
  UEFI_GUI_CONFIG  Config;

  UefiGuiConfigInitDefaults (&Config);
  Config.Display.Backend = Backend;
  if (DrawLines != 0) {
    Config.Display.DrawBufferLines = DrawLines;
  }

  Config.Display.PreferredWidth  = Width;
  Config.Display.PreferredHeight = Height;
  return UefiGuiInitialize (&Config);
}

UINT32
Random32 (
  VOID
  )
{
  mRandom ^= mRandom << 13;
  mRandom ^= mRandom >> 17;
  mRandom ^= mRandom << 5;
  return mRandom;
}

// ---------------------------------------------------------------------------
// Scene.
// ---------------------------------------------------------------------------

STATIC CONST CHAR8  *mListItems[] = {
  "Boot Manager", "Device Manager", "Secure Boot", "Network Stack",
  "Storage", "Power", "Date and Time", "Language"
};

VOID
BuildScene (
  OUT PERF_SCENE  *Scene
  )
{
  lv_obj_t           *Screen;
  lv_obj_t           *Row;
  lv_obj_t           *Body;
  lv_obj_t           *Left;
  lv_obj_t           *Right;
  lv_obj_t           *List;
  lv_obj_t           *Obj;
  lv_chart_series_t  *Series[2];
  UINTN              Index;

  ZeroMem (Scene, sizeof (*Scene));
  Screen = lv_screen_active ();
  lv_obj_clean (Screen);
  lv_obj_set_style_bg_color (Screen, lv_color_hex (PERF_SCENE_BG), 0);
  lv_obj_set_style_pad_all (Screen, 0, 0);

  Scene->Root = lv_obj_create (Screen);
  lv_obj_set_size (Scene->Root, lv_pct (96), lv_pct (94));
  lv_obj_align (Scene->Root, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_flex_flow (Scene->Root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all (Scene->Root, 8, 0);
  lv_obj_set_style_pad_row (Scene->Root, 8, 0);
  lv_obj_set_scrollable (Scene->Root, false);

  Scene->Title = lv_label_create (Scene->Root);
  lv_obj_set_style_text_font (Scene->Title, &lv_font_montserrat_28, 0);
  lv_label_set_text (Scene->Title, "PerfTest - LVGLUefiGuiPkg Phase 6");

  Row = lv_obj_create (Scene->Root);
  lv_obj_set_size (Row, lv_pct (100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Row, 8, 0);
  for (Index = 0; Index < 6; Index++) {
    Scene->Button[Index]      = lv_button_create (Row);
    Scene->ButtonLabel[Index] = lv_label_create (Scene->Button[Index]);
    lv_label_set_text_fmt (Scene->ButtonLabel[Index], "Button %u", (UINT32)Index);
    lv_obj_set_width (Scene->Button[Index], lv_pct (15));
  }

  Body = lv_obj_create (Scene->Root);
  lv_obj_set_width (Body, lv_pct (100));
  lv_obj_set_flex_grow (Body, 1);
  lv_obj_set_flex_flow (Body, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Body, 8, 0);

  Left = lv_obj_create (Body);
  lv_obj_set_size (Left, lv_pct (48), lv_pct (100));
  lv_obj_set_flex_flow (Left, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row (Left, 6, 0);

  Scene->TextArea = lv_textarea_create (Left);
  lv_obj_set_size (Scene->TextArea, lv_pct (100), lv_pct (30));
  lv_textarea_set_placeholder_text (Scene->TextArea, "Type here");

  //
  // A list built from a flex column (lv_list is deprecated in LVGL 9.6).
  //
  List = lv_obj_create (Left);
  lv_obj_set_width (List, lv_pct (100));
  lv_obj_set_flex_grow (List, 1);
  lv_obj_set_flex_flow (List, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row (List, 2, 0);
  for (Index = 0; Index < ARRAY_SIZE (mListItems); Index++) {
    Obj = lv_button_create (List);
    lv_obj_set_width (Obj, lv_pct (100));
    lv_label_set_text_fmt (lv_label_create (Obj), LV_SYMBOL_RIGHT "  %s", mListItems[Index]);
  }

  Obj = lv_checkbox_create (Left);
  lv_checkbox_set_text (Obj, "Enable logging");
  lv_obj_add_state (Obj, LV_STATE_CHECKED);

  Right = lv_obj_create (Body);
  lv_obj_set_size (Right, lv_pct (48), lv_pct (100));
  lv_obj_set_flex_flow (Right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row (Right, 6, 0);

  Scene->Chart = lv_chart_create (Right);
  lv_obj_set_size (Scene->Chart, lv_pct (100), lv_pct (35));
  lv_chart_set_point_count (Scene->Chart, 40);
  Series[0] = lv_chart_add_series (Scene->Chart, lv_palette_main (LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
  Series[1] = lv_chart_add_series (Scene->Chart, lv_palette_main (LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
  for (Index = 0; Index < 40; Index++) {
    lv_chart_set_next_value (Scene->Chart, Series[0], (INT32)(50 + (Random32 () % 40)));
    lv_chart_set_next_value (Scene->Chart, Series[1], (INT32)(10 + (Random32 () % 30)));
  }

  Scene->Table = lv_table_create (Right);
  lv_obj_set_width (Scene->Table, lv_pct (100));
  lv_obj_set_flex_grow (Scene->Table, 1);
  lv_table_set_column_count (Scene->Table, 3);
  lv_table_set_row_count (Scene->Table, 4);
  for (Index = 0; Index < 4; Index++) {
    lv_table_set_cell_value_fmt (Scene->Table, (UINT32)Index, 0, "Item %u", (UINT32)Index);
    lv_table_set_cell_value_fmt (Scene->Table, (UINT32)Index, 1, "%u", (UINT32)(Index * 17 + 3));
    lv_table_set_cell_value (Scene->Table, (UINT32)Index, 2, (Index & 1) ? "OK" : "Ready");
  }

  Scene->Bar = lv_bar_create (Right);
  lv_obj_set_width (Scene->Bar, lv_pct (100));
  lv_bar_set_value (Scene->Bar, 40, LV_ANIM_OFF);

  Obj = lv_slider_create (Right);
  lv_obj_set_width (Obj, lv_pct (90));
  lv_slider_set_value (Obj, 70, LV_ANIM_OFF);

  Scene->Churn = lv_obj_create (Right);
  lv_obj_set_size (Scene->Churn, lv_pct (100), 60);
  lv_obj_set_flex_flow (Scene->Churn, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_all (Scene->Churn, 2, 0);

  Scene->Status = lv_label_create (Scene->Root);
  lv_label_set_text (Scene->Status, "ready");
}

VOID
RefreshMeasured (
  IN  BOOLEAN  FullScreen,
  OUT UINT64   *TotalUs,
  OUT UINT64   *FlushUs,
  OUT UINT64   *Pixels
  )
{
  UEFI_GUI_RENDER_STATS  Before;
  UEFI_GUI_RENDER_STATS  After;
  UINT64                 Start;

  if (FullScreen) {
    lv_obj_invalidate (lv_screen_active ());
  }

  UefiGuiGetRenderStats (&Before);
  Start = UefiGuiGetTimeUs ();
  lv_refr_now (NULL);
  *TotalUs = UefiGuiGetTimeUs () - Start;
  UefiGuiGetRenderStats (&After);
  *FlushUs = After.FlushTimeUs - Before.FlushTimeUs;
  *Pixels  = After.FlushedPixels - Before.FlushedPixels;
}

// ---------------------------------------------------------------------------
// Entry point.
// ---------------------------------------------------------------------------

/**
  Read "-soak N" from the shell command line.
**/
STATIC
UINT32
SoakSecondsFromCommandLine (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Image;
  CHAR16                     *Text;
  UINTN                      Length;
  UINTN                      Index;
  UINT32                     Seconds;

  Seconds = 60;
  if (EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image)) ||
      (Image->LoadOptions == NULL) || (Image->LoadOptionsSize < sizeof (CHAR16)))
  {
    return Seconds;
  }

  Text   = (CHAR16 *)Image->LoadOptions;
  Length = Image->LoadOptionsSize / sizeof (CHAR16);
  for (Index = 0; Index + 6 < Length && Text[Index] != L'\0'; Index++) {
    if ((Text[Index] == L'-') && (Text[Index + 1] == L's') && (Text[Index + 2] == L'o') &&
        (Text[Index + 3] == L'a') && (Text[Index + 4] == L'k') && (Text[Index + 5] == L' '))
    {
      Index  += 6;
      Seconds = 0;
      while (Index < Length && Text[Index] >= L'0' && Text[Index] <= L'9') {
        Seconds = Seconds * 10 + (UINT32)(Text[Index] - L'0');
        Index++;
      }

      break;
    }
  }

  if (Seconds < 10) {
    Seconds = 10;
  }

  return Seconds;
}

EFI_STATUS
EFIAPI
PerfTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT64                        Baseline;
  UINT64                        FreeBefore;
  UINT64                        FreeAfter;
  UINT32                        Soak;
  UINTN                         Index;
  UINTN                         Failures;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  Soak = SoakSecondsFromCommandLine (ImageHandle);
  //
  // The GOP section runs before UefiGuiInitialize(); start the time base.
  //
  Check (L"time base initialized", (BOOLEAN)!EFI_ERROR (UefiGuiTimeInitialize ()));
  Print (L"PerfTest - LVGLUefiGuiPkg Phase 6 (soak %u s)\n", Soak);
  gBS->LocateProtocol (&gEfiSerialIoProtocolGuid, NULL, (VOID **)&mSerial);
  Check (L"serial port for host commands", (BOOLEAN)(mSerial != NULL));
  Gop = FindGop ();
  Check (L"GOP present", (BOOLEAN)(Gop != NULL));
  if (Gop == NULL) {
    Print (L"UEFIGUI-PERFTEST: FAIL\n");
    return EFI_NOT_FOUND;
  }

  Metric ("platform.time_frequency", UefiGuiGetTimeFrequency (), "Hz");
  Metric ("platform.gop.mode_count", Gop->Mode->MaxMode, "n");
  Metric ("platform.gop.start_width", Gop->Mode->Info->HorizontalResolution, "px");
  Metric ("platform.gop.start_height", Gop->Mode->Info->VerticalResolution, "px");
  Metric ("platform.gop.pixel_format", Gop->Mode->Info->PixelFormat, "enum");
  Metric ("platform.gop.frame_buffer_size", Gop->Mode->FrameBufferSize, "B");

  Baseline   = GuiBytes ();
  FreeBefore = FreeSystemKib ();

  SectionGop ();
  SectionUpdatesAndMemory ();
  SectionInputLatency ();
  SectionResolutions ();
  SectionSoak (Soak);
  SectionSummaryScreen ();

  CheckValue (L"end: UefiGui memory back to the start value", (BOOLEAN)(GuiBytes () == Baseline), (INT64)(GuiBytes () - Baseline));
  FreeAfter = FreeSystemKib ();
  Metric ("memory.system.free_delta_after_run", (FreeBefore >= FreeAfter) ? FreeBefore - FreeAfter : 0, "KiB");

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

  for (Index = 0; Index < mMetricCount; Index++) {
    Print (L"UEFIGUI-PERF %a %lu %a\n", mMetrics[Index].Key, mMetrics[Index].Value, mMetrics[Index].Unit);
  }

  Print (L"%lu checks, %lu failed, %lu measurements\n", (UINT64)mResultCount, (UINT64)Failures, (UINT64)mMetricCount);
  Print (L"UEFIGUI-PERFTEST: %s\n", (Failures == 0) ? L"PASS" : L"FAIL");
  return (Failures == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
