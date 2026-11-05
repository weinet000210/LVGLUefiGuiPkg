/** @file
  UefiGuiDisplayLib - GOP display backend.

  INTERFACE DRAFT (Phase 0). Implemented in Phase 1; signatures may still
  change during Phase 1 review.

  The display backend owns the Graphics Output Protocol. It never assumes a
  directly accessible frame buffer: the default path converts pixels from a
  source buffer into EFI_GRAPHICS_OUTPUT_BLT_PIXEL rows and copies them with
  GOP Blt(EfiBltBufferToVideo), which also works in PixelBltOnly mode.

  This library knows nothing about LVGL. UefiGuiLib connects it to the LVGL
  flush callback.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_DISPLAY_LIB_H_
#define UEFI_GUI_DISPLAY_LIB_H_

#include <UefiGui/UefiGuiTypes.h>
#include <UefiGui/UefiGuiConfig.h>

///
/// Pixel layout of a source buffer handed to UefiGuiDisplayFlushRect().
/// Values describe memory byte order, never a CPU word.
///
typedef enum {
  ///
  /// 4 bytes per pixel: Blue, Green, Red, unused. (LVGL XRGB8888 on a
  /// little-endian CPU.)
  ///
  UefiGuiSourceFormatXrgb8888 = 0,
  ///
  /// 2 bytes per pixel, little-endian 16-bit word RRRRRGGGGGGBBBBB.
  ///
  UefiGuiSourceFormatRgb565,
  UefiGuiSourceFormatMax
} UEFI_GUI_SOURCE_FORMAT;

/**
  Locate GOP, optionally switch mode, validate the mode and allocate the
  conversion buffer.

  @retval EFI_SUCCESS            Display ready.
  @retval EFI_NOT_FOUND          No GOP instance.
  @retval EFI_UNSUPPORTED        The mode cannot be driven.
  @retval EFI_OUT_OF_RESOURCES   Allocation failed.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayInitialize (
  IN CONST UEFI_GUI_DISPLAY_CONFIG  *Config
  );

/**
  Copy the active mode description.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayGetInfo (
  OUT UEFI_GUI_DISPLAY_INFO  *Info
  );

/**
  Convert and draw a rectangle.

  @param[in] Source             First pixel of the rectangle.
  @param[in] Format             Layout of Source.
  @param[in] SourceStrideBytes  Bytes between two rows of Source.
  @param[in] DestX, DestY       Top-left destination pixel (inclusive).
  @param[in] Width, Height      Rectangle size in pixels.

  @retval EFI_SUCCESS            Drawn.
  @retval EFI_INVALID_PARAMETER  Bad arguments or rectangle outside display.
  @retval other                  Error returned by GOP Blt().
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayFlushRect (
  IN CONST VOID              *Source,
  IN UEFI_GUI_SOURCE_FORMAT  Format,
  IN UINTN                   SourceStrideBytes,
  IN UINTN                   DestX,
  IN UINTN                   DestY,
  IN UINTN                   Width,
  IN UINTN                   Height
  );

/**
  Fill a rectangle with one color given as 0x00RRGGBB.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayFillRect (
  IN UINT32  Rgb,
  IN UINTN   DestX,
  IN UINTN   DestY,
  IN UINTN   Width,
  IN UINTN   Height
  );

/**
  Release buffers and restore the GOP mode that was active before
  UefiGuiDisplayInitialize() when the backend changed it.
**/
VOID
EFIAPI
UefiGuiDisplayShutdown (
  VOID
  );

#endif // UEFI_GUI_DISPLAY_LIB_H_
