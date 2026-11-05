/** @file
  UefiGuiDisplayLib - GOP display backend.

  The display backend owns the Graphics Output Protocol. It never assumes a
  directly accessible frame buffer: the default path converts pixels from a
  source buffer into EFI_GRAPHICS_OUTPUT_BLT_PIXEL rows and copies them with
  GOP Blt(EfiBltBufferToVideo), which also works in PixelBltOnly mode.

  Backends:
    - Blt (default, UefiGuiDisplayBackendAuto resolves to it): every pixel
      format, including PixelBltOnly and PixelBitMask, because GOP Blt()
      always takes EFI_GRAPHICS_OUTPUT_BLT_PIXEL and the firmware converts.
    - FrameBuffer (only when explicitly requested): writes the frame buffer
      directly in its native format (RGB8, BGR8 or PixelBitMask with 16 or
      32 bits per pixel). Initialization fails with EFI_UNSUPPORTED unless
      the frame buffer passes validation: non-zero base, size covering
      PixelsPerScanLine * height, GCD memory descriptors covering the range
      without EFI_MEMORY_RO / EFI_MEMORY_RP, and a write/read-back probe.

  All functions must be called at TPL_APPLICATION from the GUI context.

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
  Encode one 0x00RRGGBB color in the native frame buffer format of a GOP
  mode. Exposed for tests and for the frame buffer backend.

  @param[in]  Rgb          Color as 0x00RRGGBB.
  @param[in]  PixelFormat  GOP pixel format.
  @param[in]  PixelMask    Channel masks, used for PixelBitMask only.

  @return  The pixel value, or 0 for PixelBltOnly or invalid masks.
**/
UINT32
EFIAPI
UefiGuiDisplayEncodePixel (
  IN UINT32                     Rgb,
  IN EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat,
  IN CONST EFI_PIXEL_BITMASK    *PixelMask  OPTIONAL
  );

/**
  Locate GOP, optionally switch mode, validate the mode and allocate the
  conversion buffer.

  The GOP on gST->ConsoleOutHandle is preferred (it reaches every console
  display); otherwise the first GOP in the handle database is used.

  @param[in]  Config   Display options, or NULL for defaults.

  @retval EFI_SUCCESS            Display ready.
  @retval EFI_ALREADY_STARTED    Already initialized.
  @retval EFI_NOT_FOUND          No GOP instance.
  @retval EFI_UNSUPPORTED        The mode, or the requested backend, cannot be
                                 driven.
  @retval EFI_INVALID_PARAMETER  Bad configuration value.
  @retval EFI_OUT_OF_RESOURCES   Allocation failed.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayInitialize (
  IN CONST UEFI_GUI_DISPLAY_CONFIG  *Config  OPTIONAL
  );

/**
  Same as UefiGuiDisplayInitialize() but drives the given GOP instance.
  Used by tests and by applications that pick a specific display.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayInitializeWithGop (
  IN CONST UEFI_GUI_DISPLAY_CONFIG  *Config  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop
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
  @param[in] DestX, DestY       Top-left destination pixel.
  @param[in] Width, Height      Rectangle size in pixels (exclusive end:
                                the rectangle covers DestX .. DestX+Width-1).

  Source must hold Height rows of SourceStrideBytes each (the last row needs
  only Width pixels). The caller passes the address of the rectangle's first
  pixel, so a sub-rectangle of a larger buffer is flushed by offsetting
  Source and keeping the larger buffer's stride.

  @retval EFI_SUCCESS            Drawn.
  @retval EFI_NOT_STARTED        Not initialized.
  @retval EFI_INVALID_PARAMETER  NULL source, zero size, bad format, stride
                                 smaller than one row, or rectangle not
                                 fully inside the display.
  @retval other                  Error returned by GOP Blt(); the rest of the
                                 rectangle is not drawn.
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
  Fill a rectangle with one color given as 0x00RRGGBB. Same rectangle rules
  and return values as UefiGuiDisplayFlushRect().
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
  Read back a rectangle of the screen as 0x00RRGGBB values (GOP Blt
  VideoToBltBuffer). Used by tests to verify what was drawn.

  @param[out] Pixels   Width * Height values, row-major, no padding.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayReadRect (
  OUT UINT32  *Pixels,
  IN  UINTN   SourceX,
  IN  UINTN   SourceY,
  IN  UINTN   Width,
  IN  UINTN   Height
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
