/** @file
  Pixel format conversion for UefiGuiDisplayLib.

  Source formats are described by memory byte order, and GOP formats by the
  UEFI specification. Every conversion goes through an explicit 0x00RRGGBB
  value, so no code relies on two layouts happening to match.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiDisplayInternal.h"

/**
  Bytes per pixel of a source format, 0 if the format is invalid.
**/
UINTN
UefiGuiSourceBytesPerPixel (
  IN UEFI_GUI_SOURCE_FORMAT  Format
  )
{
  switch (Format) {
    case UefiGuiSourceFormatXrgb8888:
      return 4;
    case UefiGuiSourceFormatRgb565:
      return 2;
    default:
      return 0;
  }
}

/**
  Read pixel Index of a source row and return it as 0x00RRGGBB.
**/
UINT32
UefiGuiSourcePixelToRgb (
  IN CONST UINT8             *Row,
  IN UEFI_GUI_SOURCE_FORMAT  Format,
  IN UINTN                   Index
  )
{
  CONST UINT8  *Pixel;
  UINT32       Word;
  UINT32       Red;
  UINT32       Green;
  UINT32       Blue;

  if (Format == UefiGuiSourceFormatXrgb8888) {
    //
    // Memory order: Blue, Green, Red, unused.
    //
    Pixel = Row + Index * 4;
    return ((UINT32)Pixel[2] << 16) | ((UINT32)Pixel[1] << 8) | (UINT32)Pixel[0];
  }

  //
  // RGB565, little-endian 16-bit word. Expand each channel to 8 bits by bit
  // replication so that full intensity maps to 0xFF.
  //
  Pixel = Row + Index * 2;
  Word  = (UINT32)Pixel[0] | ((UINT32)Pixel[1] << 8);
  Red   = (Word >> 11) & 0x1F;
  Green = (Word >> 5) & 0x3F;
  Blue  = Word & 0x1F;
  Red   = (Red << 3) | (Red >> 2);
  Green = (Green << 2) | (Green >> 4);
  Blue  = (Blue << 3) | (Blue >> 2);
  return (Red << 16) | (Green << 8) | Blue;
}

/**
  Describe one PixelBitMask channel.

  @retval TRUE   Mask is non-zero, contiguous and at most 16 bits wide.
**/
STATIC
BOOLEAN
UefiGuiMaskToChannel (
  IN  UINT32            Mask,
  OUT UEFI_GUI_CHANNEL  *Channel
  )
{
  UINT32  Shift;
  UINT32  Value;
  UINT32  Width;

  if (Mask == 0) {
    return FALSE;
  }

  Shift = 0;
  while ((Mask & (1u << Shift)) == 0) {
    Shift++;
  }

  Value = Mask >> Shift;
  if ((Value & (Value + 1)) != 0) {
    //
    // Not contiguous.
    //
    return FALSE;
  }

  Width = 0;
  while (Value != 0) {
    Width++;
    Value >>= 1;
  }

  if (Width > 16) {
    return FALSE;
  }

  Channel->Shift = Shift;
  Channel->Width = Width;
  return TRUE;
}

/**
  Derive the native layout of a GOP pixel format.

  PixelBitMask pixels are 16 bits wide when all masks fit in the low 16 bits
  and 32 bits wide otherwise. 24-bit packed pixels are not supported.

  @retval EFI_SUCCESS       Layout filled.
  @retval EFI_UNSUPPORTED   The format cannot be written directly.
**/
EFI_STATUS
UefiGuiGetPixelLayout (
  IN  EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat,
  IN  CONST EFI_PIXEL_BITMASK    *PixelMask  OPTIONAL,
  OUT UEFI_GUI_PIXEL_LAYOUT      *Layout
  )
{
  UINT32  AllMasks;

  ZeroMem (Layout, sizeof (*Layout));

  switch (PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor:
      //
      // Byte 0 red, byte 1 green, byte 2 blue.
      //
      Layout->BytesPerPixel = 4;
      Layout->Red.Shift     = 0;
      Layout->Green.Shift   = 8;
      Layout->Blue.Shift    = 16;
      Layout->Red.Width     = 8;
      Layout->Green.Width   = 8;
      Layout->Blue.Width    = 8;
      return EFI_SUCCESS;

    case PixelBlueGreenRedReserved8BitPerColor:
      //
      // Byte 0 blue, byte 1 green, byte 2 red.
      //
      Layout->BytesPerPixel = 4;
      Layout->Blue.Shift    = 0;
      Layout->Green.Shift   = 8;
      Layout->Red.Shift     = 16;
      Layout->Red.Width     = 8;
      Layout->Green.Width   = 8;
      Layout->Blue.Width    = 8;
      return EFI_SUCCESS;

    case PixelBitMask:
      if (PixelMask == NULL) {
        return EFI_UNSUPPORTED;
      }

      if (!UefiGuiMaskToChannel (PixelMask->RedMask, &Layout->Red) ||
          !UefiGuiMaskToChannel (PixelMask->GreenMask, &Layout->Green) ||
          !UefiGuiMaskToChannel (PixelMask->BlueMask, &Layout->Blue))
      {
        return EFI_UNSUPPORTED;
      }

      if (((PixelMask->RedMask & PixelMask->GreenMask) != 0) ||
          ((PixelMask->RedMask & PixelMask->BlueMask) != 0) ||
          ((PixelMask->GreenMask & PixelMask->BlueMask) != 0) ||
          (((PixelMask->RedMask | PixelMask->GreenMask | PixelMask->BlueMask) & PixelMask->ReservedMask) != 0))
      {
        return EFI_UNSUPPORTED;
      }

      AllMasks              = PixelMask->RedMask | PixelMask->GreenMask | PixelMask->BlueMask | PixelMask->ReservedMask;
      Layout->BytesPerPixel = ((AllMasks & 0xFFFF0000) != 0) ? 4 : 2;
      return EFI_SUCCESS;

    default:
      //
      // PixelBltOnly has no frame buffer; anything else is invalid.
      //
      return EFI_UNSUPPORTED;
  }
}

/**
  Scale an 8-bit channel value to Width bits, rounding to nearest.
**/
STATIC
UINT32
UefiGuiScaleFrom8 (
  IN UINT32  Value,
  IN UINT32  Width
  )
{
  UINT32  Max;

  if (Width == 8) {
    return Value;
  }

  Max = (1u << Width) - 1;
  return (Value * Max + 127) / 255;
}

/**
  Scale a Width-bit channel value to 8 bits, rounding to nearest.
**/
STATIC
UINT32
UefiGuiScaleTo8 (
  IN UINT32  Value,
  IN UINT32  Width
  )
{
  UINT32  Max;

  if (Width == 8) {
    return Value;
  }

  Max = (1u << Width) - 1;
  return (Value * 255 + Max / 2) / Max;
}

/**
  Encode 0x00RRGGBB with a native layout.
**/
UINT32
UefiGuiEncodeWithLayout (
  IN UINT32                       Rgb,
  IN CONST UEFI_GUI_PIXEL_LAYOUT  *Layout
  )
{
  UINT32  Pixel;

  Pixel  = UefiGuiScaleFrom8 ((Rgb >> 16) & 0xFF, Layout->Red.Width) << Layout->Red.Shift;
  Pixel |= UefiGuiScaleFrom8 ((Rgb >> 8) & 0xFF, Layout->Green.Width) << Layout->Green.Shift;
  Pixel |= UefiGuiScaleFrom8 (Rgb & 0xFF, Layout->Blue.Width) << Layout->Blue.Shift;
  return Pixel;
}

/**
  Decode a native pixel to 0x00RRGGBB.
**/
UINT32
UefiGuiDecodeWithLayout (
  IN UINT32                       Pixel,
  IN CONST UEFI_GUI_PIXEL_LAYOUT  *Layout
  )
{
  UINT32  Red;
  UINT32  Green;
  UINT32  Blue;

  Red   = (Pixel >> Layout->Red.Shift) & ((1u << Layout->Red.Width) - 1);
  Green = (Pixel >> Layout->Green.Shift) & ((1u << Layout->Green.Width) - 1);
  Blue  = (Pixel >> Layout->Blue.Shift) & ((1u << Layout->Blue.Width) - 1);
  return (UefiGuiScaleTo8 (Red, Layout->Red.Width) << 16) |
         (UefiGuiScaleTo8 (Green, Layout->Green.Width) << 8) |
         UefiGuiScaleTo8 (Blue, Layout->Blue.Width);
}

/**
  Encode one 0x00RRGGBB color in the native frame buffer format of a GOP
  mode.

  @return  The pixel value, or 0 for PixelBltOnly or invalid masks.
**/
UINT32
EFIAPI
UefiGuiDisplayEncodePixel (
  IN UINT32                     Rgb,
  IN EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat,
  IN CONST EFI_PIXEL_BITMASK    *PixelMask  OPTIONAL
  )
{
  UEFI_GUI_PIXEL_LAYOUT  Layout;

  if (EFI_ERROR (UefiGuiGetPixelLayout (PixelFormat, PixelMask, &Layout))) {
    return 0;
  }

  return UefiGuiEncodeWithLayout (Rgb, &Layout);
}

/**
  Validate a rectangle against the display, with overflow checks.

  @retval TRUE  Width and Height are non-zero and the rectangle lies fully
                inside the active mode.
**/
BOOLEAN
UefiGuiRectIsInside (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  UINTN  DisplayWidth;
  UINTN  DisplayHeight;

  DisplayWidth  = gUefiGuiDisplay.Info.HorizontalResolution;
  DisplayHeight = gUefiGuiDisplay.Info.VerticalResolution;

  if ((Width == 0) || (Height == 0)) {
    return FALSE;
  }

  if ((X >= DisplayWidth) || (Y >= DisplayHeight)) {
    return FALSE;
  }

  //
  // Written as subtractions so X + Width cannot overflow.
  //
  return (BOOLEAN)((Width <= DisplayWidth - X) && (Height <= DisplayHeight - Y));
}
