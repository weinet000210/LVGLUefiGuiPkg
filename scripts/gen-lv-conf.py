#!/usr/bin/env python3
## @file
#  Regenerate Library/LvglLib/lv_conf.h from the LVGL submodule's
#  lv_conf_template.h, applying the LVGLUefiGuiPkg settings below.
#
#  Every setting must exist in the template; the script fails loudly when an
#  LVGL update renames or removes one, instead of silently keeping a default.
#
#  Usage: python3 scripts/gen-lv-conf.py
#
#  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

import os
import re
import sys

PKG = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(PKG, "Library", "LvglLib")

HEADER = """/**
 * @file lv_conf.h
 * LVGL v9.6.0 configuration for LVGLUefiGuiPkg.
 *
 * GENERATED from lvgl/lv_conf_template.h by scripts/gen-lv-conf.py; change
 * the script, not this file.
 *
 * Choices (see Docs/Architecture.md, "LVGL configuration"):
 *   - single-threaded, no OS (LV_OS_NONE), software rendering only
 *   - memory: LV_STDLIB_CUSTOM -> UefiGuiAllocate()/UefiGuiFree()
 *     (implemented in Library/UefiGuiLib/UefiGuiLvglPort.c)
 *   - string and sprintf: LVGL built-in implementations (no libc)
 *   - <inttypes.h>/<limits.h>: freestanding replacements in this directory
 *   - no file system drivers, no image decoders, no LVGL UEFI driver
 *     (LVGLUefiGuiPkg provides its own GOP and input backends)
 *   - fonts: Montserrat 14/20/28 for the UI, unscii 8/16 monospace, ASCII
 *   - logging and asserts routed to EDK2 DEBUG() / lv_uefi_gui_assert.h
 *   - LV_USE_SYSMON / LV_USE_PERF_MONITOR follow UEFI_GUI_PERF_MONITOR
 *     (default 0). With the performance monitor compiled in, LVGL never
 *     pauses its display refresh timer, so the event loop wakes every
 *     LV_DEF_REFR_PERIOD even on an idle screen. Enable it for measurements
 *     only, by building with -DUEFI_GUI_PERF_MONITOR=1 (see BuildGuide.md).
 */
#ifndef UEFI_GUI_PERF_MONITOR
#define UEFI_GUI_PERF_MONITOR 0
#endif
"""

SETTINGS = [
    ("LV_USE_STDLIB_MALLOC", "LV_STDLIB_CUSTOM"),
    ("LV_INTTYPES_INCLUDE", '"lv_uefi_gui_inttypes.h"'),
    ("LV_LIMITS_INCLUDE", '"lv_uefi_gui_limits.h"'),
    ("LV_USE_OS", "LV_OS_NONE"),
    ("LV_DEF_REFR_PERIOD", "16"),
    # Only the formats UefiGui renders to or LVGL needs internally.
    ("LV_DRAW_SW_SUPPORT_RGB565_SWAPPED", "0"),
    ("LV_DRAW_SW_SUPPORT_RGB565A8", "0"),
    ("LV_DRAW_SW_SUPPORT_RGB888", "0"),
    ("LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED", "0"),
    ("LV_DRAW_SW_SUPPORT_L8", "0"),
    ("LV_DRAW_SW_SUPPORT_AL88", "0"),
    ("LV_DRAW_SW_SUPPORT_I1", "0"),
    ("LV_USE_LOG", "1"),
    ("LV_LOG_LEVEL", "LV_LOG_LEVEL_WARN"),
    ("LV_LOG_PRINTF", "0"),
    ("LV_USE_ASSERT_NULL", "1"),
    ("LV_USE_ASSERT_MALLOC", "1"),
    ("LV_ASSERT_USE_CUSTOM_INCLUDE", "1"),
    ("LV_ASSERT_CUSTOM_INCLUDE", '"lv_uefi_gui_assert.h"'),
    ("LV_FONT_MONTSERRAT_20", "1"),
    ("LV_FONT_MONTSERRAT_28", "1"),
    ("LV_FONT_UNSCII_8", "1"),
    ("LV_FONT_UNSCII_16", "1"),
    ("LV_USE_SYSMON", "UEFI_GUI_PERF_MONITOR"),
    ("LV_USE_PERF_MONITOR", "UEFI_GUI_PERF_MONITOR"),
    ("LV_BUILD_EXAMPLES", "0"),
    ("LV_BUILD_DEMOS", "0"),
]


def main():
    with open(os.path.join(LIB, "lvgl", "lv_conf_template.h")) as fh:
        text = fh.read()

    enable = '#if 0 /* Set this to "1" to enable content */'
    if enable not in text:
        sys.exit("template: enable switch not found")
    text = text.replace(enable, '#if 1 /* Set this to "1" to enable content */', 1)

    for name, value in SETTINGS:
        pattern = re.compile(r"^(\s*#define " + re.escape(name) + r") .*$", re.M)
        text, count = pattern.subn(lambda m: m.group(1) + " " + value, text, count=1)
        if count != 1:
            sys.exit("template: %s not found" % name)

    with open(os.path.join(LIB, "lv_conf.h"), "w", newline="\n") as fh:
        fh.write(HEADER + text)
    print("lv_conf.h: %d settings applied" % len(SETTINGS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
