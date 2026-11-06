/** @file
  PerfTest - Phase 6 performance and stability measurements.

  Shared declarations of the PerfTest sections.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef PERF_TEST_H_
#define PERF_TEST_H_

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
#include <Library/UefiGuiDisplayLib.h>
#include <Library/UefiGuiInputLib.h>
#include <Library/UefiGuiRuntimeLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SerialIo.h>

#define PERF_MAX_SAMPLES  4096

///
/// A set of timing samples in microseconds.
///
typedef struct {
  UINT64    Value[PERF_MAX_SAMPLES];
  UINTN     Count;
} PERF_SAMPLES;

typedef struct {
  UINT64    Min;
  UINT64    Median;
  UINT64    P95;
  UINT64    Max;
  UINT64    Mean;
} PERF_SUMMARY;

//
// PerfTest.c: results, metrics and helpers.
//

/**
  Record a pass/fail check, optionally with a value.
**/
VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed
  );

VOID
CheckValue (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed,
  IN INT64         Value
  );

/**
  Record a measurement. Key is ASCII without spaces; it is printed as
  "UEFIGUI-PERF <Key> <Value> <Unit>" at the end of the run and read by
  scripts/perf-report.py.
**/
VOID
Metric (
  IN CONST CHAR8  *Key,
  IN UINT64       Value,
  IN CONST CHAR8  *Unit
  );

/**
  Record a measurement whose key is built with AsciiSPrint-style format.
**/
VOID
EFIAPI
MetricF (
  IN UINT64       Value,
  IN CONST CHAR8  *Unit,
  IN CONST CHAR8  *KeyFormat,
  ...
  );

/**
  Record min, median, p95, max and mean of Samples as <Prefix>.min ... .mean.
**/
VOID
MetricSummary (
  IN CONST CHAR8         *Prefix,
  IN CONST PERF_SAMPLES  *Samples,
  OUT PERF_SUMMARY       *Summary OPTIONAL
  );

BOOLEAN
MetricGet (
  IN  CONST CHAR8  *Key,
  OUT UINT64       *Value
  );

VOID
Summarize (
  IN  CONST PERF_SAMPLES  *Samples,
  OUT PERF_SUMMARY        *Summary
  );

VOID
SampleAdd (
  IN OUT PERF_SAMPLES  *Samples,
  IN UINT64            Value
  );

/**
  Write one "UEFIGUI-CMD" host command line to the serial port.
**/
VOID
SendCmd (
  IN CONST CHAR8  *Commands
  );

/**
  Run the event loop for Ms milliseconds.
**/
VOID
Pump (
  IN UINT64  Ms
  );

/**
  Bytes currently allocated through UefiGuiAllocate().
**/
UINT64
GuiBytes (
  VOID
  );

/**
  Free EfiConventionalMemory in KiB, from the UEFI memory map.
**/
UINT64
FreeSystemKib (
  VOID
  );

/**
  The GOP the framework drives (console output handle first, else the first
  GOP instance).
**/
EFI_GRAPHICS_OUTPUT_PROTOCOL *
FindGop (
  VOID
  );

/**
  Initialize UefiGui with the given display options (0 = default).
**/
EFI_STATUS
InitGui (
  IN UEFI_GUI_DISPLAY_BACKEND  Backend,
  IN UINT32                    DrawLines,
  IN UINT32                    Width,
  IN UINT32                    Height
  );

/**
  Pseudo-random number (xorshift), deterministic per run.
**/
UINT32
Random32 (
  VOID
  );

//
// Scene shared by the LVGL sections.
//
typedef struct {
  lv_obj_t    *Root;
  lv_obj_t    *Title;
  lv_obj_t    *Button[6];
  lv_obj_t    *ButtonLabel[6];
  lv_obj_t    *TextArea;
  lv_obj_t    *Bar;
  lv_obj_t    *Chart;
  lv_obj_t    *Table;
  lv_obj_t    *Churn;
  lv_obj_t    *Status;
} PERF_SCENE;

#define PERF_SCENE_BG  0x1E2A38

/**
  Build the representative screen used by every LVGL measurement: a title,
  a row of buttons, a text area, a list, a chart, a table, a bar, a slider,
  an arc and a checkbox, laid out with flex in percent of the screen.
**/
VOID
BuildScene (
  OUT PERF_SCENE  *Scene
  );

/**
  Refresh the whole screen once with lv_refr_now().

  @param[out] TotalUs   Wall time of lv_refr_now().
  @param[out] FlushUs   Time spent in the display backend (conversion + copy).
  @param[out] Pixels    Pixels handed to the display backend.
**/
VOID
RefreshMeasured (
  IN  BOOLEAN  FullScreen,
  OUT UINT64   *TotalUs,
  OUT UINT64   *FlushUs,
  OUT UINT64   *Pixels
  );

//
// Sections.
//
VOID
SectionGop (
  VOID
  );

VOID
SectionUpdatesAndMemory (
  VOID
  );

VOID
SectionInputLatency (
  VOID
  );

VOID
SectionResolutions (
  VOID
  );

VOID
SectionSoak (
  IN UINT32  Seconds
  );

VOID
SectionSummaryScreen (
  VOID
  );

#endif // PERF_TEST_H_
