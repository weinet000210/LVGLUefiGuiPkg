/** @file
  Internal definitions of UefiGuiDisplayLib.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_DISPLAY_INTERNAL_H_
#define UEFI_GUI_DISPLAY_INTERNAL_H_

#include <Uefi.h>
#include <Pi/PiDxeCis.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiGuiDisplayLib.h>
#include <Library/UefiGuiRuntimeLib.h>
#include <Protocol/GraphicsOutput.h>
#include <UefiGui/UefiGuiStatus.h>

///
/// Default height of the conversion buffer, in display lines.
///
#define UEFI_GUI_DEFAULT_DRAW_LINES  64
#define UEFI_GUI_MAX_DRAW_LINES      4096

///
/// One color channel of a native frame buffer pixel.
///
typedef struct {
  UINT32    Shift;    ///< Position of the lowest bit.
  UINT32    Width;    ///< Number of bits, 1..16 (0 = channel absent).
} UEFI_GUI_CHANNEL;

///
/// Native frame buffer layout, derived from the GOP mode.
///
typedef struct {
  UINT32              BytesPerPixel;   ///< 2 or 4.
  UEFI_GUI_CHANNEL    Red;
  UEFI_GUI_CHANNEL    Green;
  UEFI_GUI_CHANNEL    Blue;
} UEFI_GUI_PIXEL_LAYOUT;

///
/// Backend state. One display per application.
///
typedef struct {
  BOOLEAN                          Initialized;
  EFI_GRAPHICS_OUTPUT_PROTOCOL     *Gop;
  UINT32                           OriginalMode;
  BOOLEAN                          ModeChanged;
  UEFI_GUI_DISPLAY_INFO            Info;
  //
  // Blt backend: conversion buffer of BltCapacity pixels.
  //
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *BltBuffer;
  UINTN                            BltCapacity;
  //
  // Frame buffer backend.
  //
  UEFI_GUI_PIXEL_LAYOUT            Layout;
  UINT8                            *FrameBuffer;
  UINTN                            FrameBufferStride;   ///< Bytes per scan line.
} UEFI_GUI_DISPLAY_CONTEXT;

extern UEFI_GUI_DISPLAY_CONTEXT  gUefiGuiDisplay;

//
// UefiGuiBuffer.c - pixel conversion
//

/**
  Read pixel Index of a source row and return it as 0x00RRGGBB.
**/
UINT32
UefiGuiSourcePixelToRgb (
  IN CONST UINT8             *Row,
  IN UEFI_GUI_SOURCE_FORMAT  Format,
  IN UINTN                   Index
  );

/**
  Bytes per pixel of a source format, 0 if the format is invalid.
**/
UINTN
UefiGuiSourceBytesPerPixel (
  IN UEFI_GUI_SOURCE_FORMAT  Format
  );

/**
  Derive the native layout of a GOP pixel format.

  @retval EFI_SUCCESS       Layout filled.
  @retval EFI_UNSUPPORTED   PixelBltOnly, invalid, overlapping or
                            non-contiguous masks, or a pixel size other than
                            16 or 32 bits.
**/
EFI_STATUS
UefiGuiGetPixelLayout (
  IN  EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat,
  IN  CONST EFI_PIXEL_BITMASK    *PixelMask  OPTIONAL,
  OUT UEFI_GUI_PIXEL_LAYOUT      *Layout
  );

/**
  Encode 0x00RRGGBB with a native layout.
**/
UINT32
UefiGuiEncodeWithLayout (
  IN UINT32                       Rgb,
  IN CONST UEFI_GUI_PIXEL_LAYOUT  *Layout
  );

/**
  Decode a native pixel to 0x00RRGGBB.
**/
UINT32
UefiGuiDecodeWithLayout (
  IN UINT32                       Pixel,
  IN CONST UEFI_GUI_PIXEL_LAYOUT  *Layout
  );

/**
  Validate a rectangle against the display, with overflow checks.
**/
BOOLEAN
UefiGuiRectIsInside (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Width,
  IN UINTN  Height
  );

//
// UefiGuiGopBlt.c - Blt backend
//

EFI_STATUS
UefiGuiBltInitialize (
  IN UINT32  DrawLines
  );

EFI_STATUS
UefiGuiBltFlush (
  IN CONST UINT8             *Source,
  IN UEFI_GUI_SOURCE_FORMAT  Format,
  IN UINTN                   SourceStrideBytes,
  IN UINTN                   DestX,
  IN UINTN                   DestY,
  IN UINTN                   Width,
  IN UINTN                   Height
  );

EFI_STATUS
UefiGuiBltFill (
  IN UINT32  Rgb,
  IN UINTN   DestX,
  IN UINTN   DestY,
  IN UINTN   Width,
  IN UINTN   Height
  );

EFI_STATUS
UefiGuiBltRead (
  OUT UINT32  *Pixels,
  IN  UINTN   SourceX,
  IN  UINTN   SourceY,
  IN  UINTN   Width,
  IN  UINTN   Height
  );

VOID
UefiGuiBltShutdown (
  VOID
  );

//
// UefiGuiFrameBuffer.c - direct frame buffer backend
//

EFI_STATUS
UefiGuiFrameBufferInitialize (
  VOID
  );

EFI_STATUS
UefiGuiFrameBufferFlush (
  IN CONST UINT8             *Source,
  IN UEFI_GUI_SOURCE_FORMAT  Format,
  IN UINTN                   SourceStrideBytes,
  IN UINTN                   DestX,
  IN UINTN                   DestY,
  IN UINTN                   Width,
  IN UINTN                   Height
  );

EFI_STATUS
UefiGuiFrameBufferFill (
  IN UINT32  Rgb,
  IN UINTN   DestX,
  IN UINTN   DestY,
  IN UINTN   Width,
  IN UINTN   Height
  );

EFI_STATUS
UefiGuiFrameBufferRead (
  OUT UINT32  *Pixels,
  IN  UINTN   SourceX,
  IN  UINTN   SourceY,
  IN  UINTN   Width,
  IN  UINTN   Height
  );

#endif // UEFI_GUI_DISPLAY_INTERNAL_H_
