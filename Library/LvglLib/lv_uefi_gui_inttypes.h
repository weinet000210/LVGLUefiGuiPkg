/**
 * @file lv_uefi_gui_inttypes.h
 * printf format macros for LVGL in a freestanding EDK2 build
 * (LV_INTTYPES_INCLUDE in lv_conf.h).
 *
 * <inttypes.h> is a hosted C library header and is not available in
 * freestanding builds. LVGL formats with its own built-in sprintf
 * (LV_USE_STDLIB_SPRINTF = LV_STDLIB_BUILTIN), which understands the
 * hh, h, l and ll length modifiers, so 64-bit values use "ll" on every
 * compiler and data model.
 *
 * Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef LV_UEFI_GUI_INTTYPES_H
#define LV_UEFI_GUI_INTTYPES_H

#include <stdint.h>

#define PRId8   "d"
#define PRIi8   "i"
#define PRIu8   "u"
#define PRIx8   "x"
#define PRIX8   "X"
#define PRId16  "d"
#define PRIi16  "i"
#define PRIu16  "u"
#define PRIx16  "x"
#define PRIX16  "X"
#define PRId32  "d"
#define PRIi32  "i"
#define PRIu32  "u"
#define PRIx32  "x"
#define PRIX32  "X"
#define PRId64  "lld"
#define PRIi64  "lli"
#define PRIu64  "llu"
#define PRIx64  "llx"
#define PRIX64  "llX"
#define PRIdMAX "lld"
#define PRIuMAX "llu"
#define PRIxMAX "llx"

#if UINTPTR_MAX > 0xFFFFFFFFu
#define PRIdPTR "lld"
#define PRIiPTR "lli"
#define PRIuPTR "llu"
#define PRIxPTR "llx"
#define PRIXPTR "llX"
#else
#define PRIdPTR "d"
#define PRIiPTR "i"
#define PRIuPTR "u"
#define PRIxPTR "x"
#define PRIXPTR "X"
#endif

#endif /* LV_UEFI_GUI_INTTYPES_H */
