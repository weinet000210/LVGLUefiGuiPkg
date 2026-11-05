/** @file
  Configuration structures passed to UefiGuiInitialize().

  Every field has a documented default. UefiGuiConfigInitDefaults() fills a
  structure with those defaults so applications only override what they need.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_CONFIG_H_
#define UEFI_GUI_CONFIG_H_

#include <UefiGui/UefiGuiTypes.h>

///
/// Structure revision. Bump when a field is added at the end.
///
#define UEFI_GUI_CONFIG_REVISION  0x00010000

///
/// Display options.
///
typedef struct {
  ///
  /// Backend to use. Default: UefiGuiDisplayBackendAuto.
  ///
  UEFI_GUI_DISPLAY_BACKEND    Backend;
  ///
  /// Preferred resolution. 0 x 0 (default) keeps the current GOP mode.
  /// When set, the closest mode not larger than the request is selected
  /// and the original mode is restored at shutdown.
  ///
  UINT32                      PreferredWidth;
  UINT32                      PreferredHeight;
  ///
  /// Height in lines of each LVGL partial draw buffer. Default: 64.
  ///
  UINT32                      DrawBufferLines;
  ///
  /// Use two draw buffers. Default: FALSE (GOP Blt is synchronous, so a
  /// second buffer gives no overlap today).
  ///
  BOOLEAN                     DoubleBuffer;
} UEFI_GUI_DISPLAY_CONFIG;

///
/// Input options.
///
typedef struct {
  BOOLEAN    EnableKeyboard;          ///< Default: TRUE.
  BOOLEAN    EnablePointer;           ///< Simple Pointer. Default: TRUE.
  BOOLEAN    EnableAbsolutePointer;   ///< Absolute Pointer. Default: TRUE.
  BOOLEAN    ShowCursor;              ///< Draw a cursor for the pointer. Default: TRUE.
  ///
  /// Pointer speed in percent of one pixel per protocol resolution unit
  /// scaled to the display. Default: 100. Range 10..1000.
  ///
  UINT32     PointerSpeedPercent;
  ///
  /// Capacity of the unified input queue, in events. Default: 64.
  /// Range 8..1024.
  ///
  UINT32     EventQueueDepth;
} UEFI_GUI_INPUT_CONFIG;

///
/// Timer and event loop options.
///
typedef struct {
  ///
  /// Shortest and longest time the event loop sleeps between LVGL timer
  /// runs, in milliseconds. Defaults: 1 and 50.
  ///
  UINT32    MinWaitMs;
  UINT32    MaxWaitMs;
} UEFI_GUI_TIMER_CONFIG;

///
/// Memory options.
///
typedef struct {
  ///
  /// Upper bound for memory allocated through UefiGuiAllocate(), in bytes.
  /// 0 (default) means no limit beyond what the firmware pool provides.
  ///
  UINT64    LimitBytes;
} UEFI_GUI_MEMORY_CONFIG;

///
/// Debug options.
///
typedef struct {
  ///
  /// Print allocation failures and backend errors with DEBUG(). Default: TRUE.
  ///
  BOOLEAN    ReportErrors;
  ///
  /// Print a memory and timing summary at shutdown. Default: FALSE.
  ///
  BOOLEAN    PrintStatsOnExit;
  ///
  /// Show the LVGL performance monitor overlay when it is compiled in.
  /// Default: FALSE.
  ///
  BOOLEAN    ShowPerformanceMonitor;
} UEFI_GUI_DEBUG_CONFIG;

///
/// Top level configuration.
///
typedef struct {
  UINT32                     Revision;   ///< UEFI_GUI_CONFIG_REVISION.
  UEFI_GUI_DISPLAY_CONFIG    Display;
  UEFI_GUI_INPUT_CONFIG      Input;
  UEFI_GUI_TIMER_CONFIG      Timer;
  UEFI_GUI_MEMORY_CONFIG     Memory;
  UEFI_GUI_DEBUG_CONFIG      Debug;
} UEFI_GUI_CONFIG;

#endif // UEFI_GUI_CONFIG_H_
