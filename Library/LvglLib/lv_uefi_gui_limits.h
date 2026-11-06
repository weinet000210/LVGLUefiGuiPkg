/**
 * @file lv_uefi_gui_limits.h
 * <limits.h> replacement for LVGL in a freestanding EDK2 build
 * (LV_LIMITS_INCLUDE in lv_conf.h).
 *
 * GCC's own <limits.h> chains to the C library's copy with #include_next,
 * which a freestanding cross toolchain does not have. The values come from
 * compiler predefined macros on GCC/Clang and from MSVC's own <limits.h>,
 * which is self-contained.
 *
 * Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef LV_UEFI_GUI_LIMITS_H
#define LV_UEFI_GUI_LIMITS_H

#if defined(_MSC_VER) && !defined(__clang__)
#include <limits.h>
#else
#ifndef CHAR_BIT
#define CHAR_BIT    __CHAR_BIT__
#endif
#ifndef SCHAR_MAX
#define SCHAR_MAX   __SCHAR_MAX__
#define SCHAR_MIN   (-SCHAR_MAX - 1)
#define UCHAR_MAX   (SCHAR_MAX * 2 + 1)
#endif
#ifndef SHRT_MAX
#define SHRT_MAX    __SHRT_MAX__
#define SHRT_MIN    (-SHRT_MAX - 1)
#define USHRT_MAX   (SHRT_MAX * 2 + 1)
#endif
#ifndef INT_MAX
#define INT_MAX     __INT_MAX__
#define INT_MIN     (-INT_MAX - 1)
#define UINT_MAX    (INT_MAX * 2U + 1U)
#endif
#ifndef LONG_MAX
#define LONG_MAX    __LONG_MAX__
#define LONG_MIN    (-LONG_MAX - 1L)
#define ULONG_MAX   (LONG_MAX * 2UL + 1UL)
#endif
#ifndef LLONG_MAX
#define LLONG_MAX   __LONG_LONG_MAX__
#define LLONG_MIN   (-LLONG_MAX - 1LL)
#define ULLONG_MAX  (LLONG_MAX * 2ULL + 1ULL)
#endif
#endif

#endif /* LV_UEFI_GUI_LIMITS_H */
