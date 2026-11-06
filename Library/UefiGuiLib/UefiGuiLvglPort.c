/** @file
  LVGL port: lv_init(), tick, log, memory and assert hooks, and the display
  driver that connects LVGL partial rendering to UefiGuiDisplayLib.

  LVGL APIs used (checked against LVGL v9.6.0, include/lvgl/):
    lv_init / lv_deinit, lv_tick_set_cb, lv_log_register_print_cb,
    lv_display_create, lv_display_set_color_format, lv_display_set_buffers
    (LV_DISPLAY_RENDER_MODE_PARTIAL), lv_display_set_flush_cb,
    lv_display_flush_ready, lv_display_flush_is_last, lv_display_delete,
    lv_draw_buf_width_to_stride, lv_sysmon_hide_performance.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInternal.h"

//
// ---------------------------------------------------------------------------
// LV_STDLIB_CUSTOM memory hooks (declared in include/lvgl/stdlib/lv_mem.h).
// All LVGL memory comes from UefiGuiAllocate() and goes back to UefiGuiFree().
// ---------------------------------------------------------------------------
//

void
lv_mem_init (
  void
  )
{
}

void
lv_mem_deinit (
  void
  )
{
}

void *
lv_malloc_core (
  size_t  size
  )
{
  return UefiGuiAllocate ((UINTN)size);
}

void *
lv_realloc_core (
  void    *p,
  size_t  new_size
  )
{
  return UefiGuiReallocate (p, (UINTN)new_size);
}

void
lv_free_core (
  void  *p
  )
{
  UefiGuiFree (p);
}

void
lv_mem_monitor_core (
  lv_mem_monitor_t  *mon_p
  )
{
  UEFI_GUI_MEMORY_STATS  Stats;

  lv_memzero (mon_p, sizeof (*mon_p));
  if (EFI_ERROR (UefiGuiGetMemoryStats (&Stats))) {
    return;
  }

  mon_p->total_size = (size_t)Stats.LimitBytes;
  mon_p->used_cnt   = (size_t)(Stats.AllocationCount - Stats.FreeCount);
  mon_p->max_used   = (size_t)Stats.PeakBytes;
  if (Stats.LimitBytes != 0) {
    mon_p->free_size = (size_t)(Stats.LimitBytes - Stats.CurrentBytes);
    mon_p->used_pct  = (uint8_t)DivU64x64Remainder (MultU64x32 (Stats.CurrentBytes, 100), Stats.LimitBytes, NULL);
  }
}

lv_result_t
lv_mem_test_core (
  void
  )
{
  UEFI_GUI_MEMORY_STATS  Stats;

  if (EFI_ERROR (UefiGuiGetMemoryStats (&Stats)) || (Stats.InvalidFreeCount != 0)) {
    return LV_RESULT_INVALID;
  }

  return LV_RESULT_OK;
}

//
// ---------------------------------------------------------------------------
// Assert, log and tick hooks.
// ---------------------------------------------------------------------------
//

/**
  LV_ASSERT_HANDLER (see Library/LvglLib/lv_uefi_gui_assert.h). Does not
  return: LVGL state is no longer consistent.
**/
void
lv_uefi_gui_assert_failed (
  const char  *file,
  int         line
  )
{
  DEBUG ((DEBUG_ERROR, "[LVGL] assertion failed at %a:%d\n", file, line));
  Print (L"LVGL assertion failed at %a:%d\n", file, line);
  CpuDeadLoop ();
}

STATIC
void
UefiGuiLvglLog (
  lv_log_level_t  level,
  const char      *buf
  )
{
  DEBUG (((level >= LV_LOG_LEVEL_ERROR) ? DEBUG_ERROR : DEBUG_WARN, "[LVGL] %a", buf));
}

STATIC
uint32_t
UefiGuiLvglTick (
  void
  )
{
  //
  // lv_tick is 32-bit and LVGL handles its wrap-around.
  //
  return (uint32_t)UefiGuiGetTimeMs ();
}

//
// ---------------------------------------------------------------------------
// Display driver.
// ---------------------------------------------------------------------------
//

/**
  LVGL flush callback. Area coordinates are inclusive; px_map holds the
  area's pixels in XRGB8888 with LVGL's stride for that width.

  lv_display_flush_ready() is called on every path, including errors, so
  LVGL never waits for a flush that will not complete.
**/
STATIC
void
UefiGuiFlushCb (
  lv_display_t     *disp,
  const lv_area_t  *area,
  uint8_t          *px_map
  )
{
  EFI_STATUS  Status;
  UINT64      Start;
  UINTN       Width;
  UINTN       Height;
  UINT32      Stride;

  Start = UefiGuiGetTimeUs ();
  gUefiGui.Stats.FlushCount++;

  if ((area == NULL) || (px_map == NULL) ||
      (area->x1 < 0) || (area->y1 < 0) || (area->x2 < area->x1) || (area->y2 < area->y1))
  {
    Status = EFI_INVALID_PARAMETER;
  } else {
    Width  = (UINTN)(area->x2 - area->x1) + 1;
    Height = (UINTN)(area->y2 - area->y1) + 1;
    Stride = lv_draw_buf_width_to_stride ((uint32_t)Width, LV_COLOR_FORMAT_XRGB8888);
    if (gUefiGui.InjectedFlushFailures != 0) {
      gUefiGui.InjectedFlushFailures--;
      Status = EFI_DEVICE_ERROR;
    } else {
      //
      // UefiGuiDisplayFlushRect() rejects areas outside the display, so a
      // bad area can never write out of bounds.
      //
      Status = UefiGuiDisplayFlushRect (
                 px_map,
                 UefiGuiSourceFormatXrgb8888,
                 Stride,
                 (UINTN)area->x1,
                 (UINTN)area->y1,
                 Width,
                 Height
                 );
    }

    if (!EFI_ERROR (Status)) {
      gUefiGui.Stats.FlushedPixels += (UINT64)Width * Height;
    }
  }

  if (EFI_ERROR (Status)) {
    gUefiGui.Stats.FlushErrors++;
    UefiGuiRecordError ("flush", Status);
  }

  if (lv_display_flush_is_last (disp)) {
    gUefiGui.Stats.FrameCount++;
  }

  gUefiGui.Stats.FlushTimeUs += UefiGuiGetTimeUs () - Start;
  lv_display_flush_ready (disp);
}

/**
  lv_init(), hooks, display creation and draw buffers.
**/
EFI_STATUS
UefiGuiLvglInitialize (
  VOID
  )
{
  UEFI_GUI_DISPLAY_INFO  Info;
  EFI_STATUS             Status;
  UINT32                 Lines;
  UINT32                 Stride;
  UINT64                 Size;

  Status = UefiGuiDisplayGetInfo (&Info);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Info.HorizontalResolution > 0x7FFF) || (Info.VerticalResolution > 0x7FFF)) {
    //
    // lv_coord is int32 but LVGL limits coordinates to LV_COORD_MAX.
    //
    return EFI_UNSUPPORTED;
  }

  lv_init ();
  gUefiGui.LvglInitialized = TRUE;
  lv_tick_set_cb (UefiGuiLvglTick);
  lv_log_register_print_cb (UefiGuiLvglLog);

  //
  // Partial rendering: each draw buffer covers DrawBufferLines full lines.
  //
  Lines  = MIN (gUefiGui.Config.Display.DrawBufferLines, Info.VerticalResolution);
  Stride = lv_draw_buf_width_to_stride (Info.HorizontalResolution, LV_COLOR_FORMAT_XRGB8888);
  Size   = MultU64x32 (Stride, Lines);
  if (Size > MAX_UINT32) {
    return EFI_UNSUPPORTED;
  }

  gUefiGui.DrawBufferSize = (UINT32)Size;
  gUefiGui.DrawBuffer[0]  = UefiGuiAllocate (gUefiGui.DrawBufferSize);
  if (gUefiGui.Config.Display.DoubleBuffer) {
    gUefiGui.DrawBuffer[1] = UefiGuiAllocate (gUefiGui.DrawBufferSize);
  }

  if ((gUefiGui.DrawBuffer[0] == NULL) ||
      (gUefiGui.Config.Display.DoubleBuffer && (gUefiGui.DrawBuffer[1] == NULL)))
  {
    return EFI_OUT_OF_RESOURCES;
  }

  gUefiGui.Display = lv_display_create ((int32_t)Info.HorizontalResolution, (int32_t)Info.VerticalResolution);
  if (gUefiGui.Display == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  lv_display_set_color_format (gUefiGui.Display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers (
    gUefiGui.Display,
    gUefiGui.DrawBuffer[0],
    gUefiGui.DrawBuffer[1],
    gUefiGui.DrawBufferSize,
    LV_DISPLAY_RENDER_MODE_PARTIAL
    );
  lv_display_set_flush_cb (gUefiGui.Display, UefiGuiFlushCb);

 #if LV_USE_PERF_MONITOR
  if (!gUefiGui.Config.Debug.ShowPerformanceMonitor) {
    lv_sysmon_hide_performance (gUefiGui.Display);
  }

 #endif

  return EFI_SUCCESS;
}

/**
  Delete the display, lv_deinit(), free the draw buffers.
**/
VOID
UefiGuiLvglShutdown (
  VOID
  )
{
  if (gUefiGui.Display != NULL) {
    lv_display_delete (gUefiGui.Display);
    gUefiGui.Display = NULL;
  }

  if (gUefiGui.LvglInitialized) {
    lv_deinit ();
    gUefiGui.LvglInitialized = FALSE;
  }

  UefiGuiFree (gUefiGui.DrawBuffer[0]);
  UefiGuiFree (gUefiGui.DrawBuffer[1]);
  gUefiGui.DrawBuffer[0] = NULL;
  gUefiGui.DrawBuffer[1] = NULL;
}
