/**
 * @file lv_uefi_gui_assert.h
 * LV_ASSERT_HANDLER for LVGLUefiGuiPkg (selected by lv_conf.h).
 *
 * A failed LVGL assertion means LVGL state is inconsistent, so the handler
 * reports the location and stops (it does not return).
 *
 * Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef LV_UEFI_GUI_ASSERT_H
#define LV_UEFI_GUI_ASSERT_H

/** Implemented in Library/UefiGuiLib/UefiGuiLvglPort.c. */
void
lv_uefi_gui_assert_failed (
  const char  *file,
  int         line
  );

#define LV_ASSERT_HANDLER  lv_uefi_gui_assert_failed (__FILE__, __LINE__);

#endif /* LV_UEFI_GUI_ASSERT_H */
