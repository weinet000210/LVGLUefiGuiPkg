/** @file
  Blt display backend: convert into EFI_GRAPHICS_OUTPUT_BLT_PIXEL rows and
  copy them with GOP Blt(). Works with every GOP pixel format, including
  PixelBltOnly, because the firmware converts Blt pixels to the device format.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiDisplayInternal.h"

//
// The XRGB8888 fast path below stores each Blt pixel as one 32-bit word.
// That relies on the layout UEFI defines for EFI_GRAPHICS_OUTPUT_BLT_PIXEL
// (Blue, Green, Red, Reserved, one byte each) and on a little-endian CPU,
// both checked here at compile time rather than assumed.
//
STATIC_ASSERT (sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL) == 4, "Blt pixel must be 4 bytes");
STATIC_ASSERT (OFFSET_OF (EFI_GRAPHICS_OUTPUT_BLT_PIXEL, Blue) == 0, "Blt pixel Blue must be byte 0");
STATIC_ASSERT (OFFSET_OF (EFI_GRAPHICS_OUTPUT_BLT_PIXEL, Green) == 1, "Blt pixel Green must be byte 1");
STATIC_ASSERT (OFFSET_OF (EFI_GRAPHICS_OUTPUT_BLT_PIXEL, Red) == 2, "Blt pixel Red must be byte 2");
STATIC_ASSERT (OFFSET_OF (EFI_GRAPHICS_OUTPUT_BLT_PIXEL, Reserved) == 3, "Blt pixel Reserved must be byte 3");

/**
  Convert one row of XRGB8888 (memory order Blue, Green, Red, unused) into
  Blt pixels, one 32-bit word per pixel, clearing the Reserved byte.
**/
STATIC
VOID
UefiGuiXrgbRowToBlt (
  IN  CONST UINT8  *SourceRow,
  OUT UINT32       *Out,
  IN  UINTN        Width
  )
{
  CONST UINT32  *Source32;
  UINTN         Column;

  if (((UINTN)SourceRow & 3) == 0) {
    Source32 = (CONST UINT32 *)SourceRow;
    for (Column = 0; Column < Width; Column++) {
      Out[Column] = Source32[Column] & 0x00FFFFFF;
    }
  } else {
    for (Column = 0; Column < Width; Column++) {
      Out[Column] = ReadUnaligned32 ((CONST UINT32 *)(SourceRow + Column * 4)) & 0x00FFFFFF;
    }
  }
}

/**
  Allocate the conversion buffer: one display width times DrawLines pixels.
**/
EFI_STATUS
UefiGuiBltInitialize (
  IN UINT32  DrawLines
  )
{
  UINTN  Width;

  Width = gUefiGuiDisplay.Info.HorizontalResolution;
  if (DrawLines > gUefiGuiDisplay.Info.VerticalResolution) {
    DrawLines = gUefiGuiDisplay.Info.VerticalResolution;
  }

  gUefiGuiDisplay.BltBuffer = UefiGuiAllocateArray (
                                Width * DrawLines,
                                sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                                );
  if (gUefiGuiDisplay.BltBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  gUefiGuiDisplay.BltCapacity = Width * DrawLines;
  return EFI_SUCCESS;
}

/**
  Store 0x00RRGGBB in a Blt pixel.
**/
STATIC
VOID
UefiGuiRgbToBlt (
  IN  UINT32                         Rgb,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Blt
  )
{
  Blt->Red      = (UINT8)(Rgb >> 16);
  Blt->Green    = (UINT8)(Rgb >> 8);
  Blt->Blue     = (UINT8)Rgb;
  Blt->Reserved = 0;
}

/**
  Convert and Blt a validated rectangle, in chunks of rows that fit the
  conversion buffer.
**/
EFI_STATUS
UefiGuiBltFlush (
  IN CONST UINT8             *Source,
  IN UEFI_GUI_SOURCE_FORMAT  Format,
  IN UINTN                   SourceStrideBytes,
  IN UINTN                   DestX,
  IN UINTN                   DestY,
  IN UINTN                   Width,
  IN UINTN                   Height
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Out;
  EFI_STATUS                     Status;
  UINTN                          RowsPerChunk;
  UINTN                          Done;
  UINTN                          Rows;
  UINTN                          Row;
  UINTN                          Column;
  CONST UINT8                    *SourceRow;

  Gop          = gUefiGuiDisplay.Gop;
  RowsPerChunk = gUefiGuiDisplay.BltCapacity / Width;
  ASSERT (RowsPerChunk >= 1);

  for (Done = 0; Done < Height; Done += Rows) {
    Rows = MIN (RowsPerChunk, Height - Done);
    Out  = gUefiGuiDisplay.BltBuffer;
    for (Row = 0; Row < Rows; Row++) {
      SourceRow = Source + (Done + Row) * SourceStrideBytes;
      if (Format == UefiGuiSourceFormatXrgb8888) {
        UefiGuiXrgbRowToBlt (SourceRow, (UINT32 *)Out, Width);
        Out += Width;
        continue;
      }

      for (Column = 0; Column < Width; Column++) {
        UefiGuiRgbToBlt (UefiGuiSourcePixelToRgb (SourceRow, Format, Column), Out);
        Out++;
      }
    }

    Status = Gop->Blt (
                    Gop,
                    gUefiGuiDisplay.BltBuffer,
                    EfiBltBufferToVideo,
                    0,
                    0,
                    DestX,
                    DestY + Done,
                    Width,
                    Rows,
                    Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                    );
    if (EFI_ERROR (Status)) {
      UEFI_GUI_LOG_ERROR ("GOP Blt(BufferToVideo)", Status);
      return Status;
    }
  }

  return EFI_SUCCESS;
}

/**
  Fill a validated rectangle with GOP Blt(VideoFill).
**/
EFI_STATUS
UefiGuiBltFill (
  IN UINT32  Rgb,
  IN UINTN   DestX,
  IN UINTN   DestY,
  IN UINTN   Width,
  IN UINTN   Height
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;
  EFI_STATUS                     Status;

  Gop = gUefiGuiDisplay.Gop;
  UefiGuiRgbToBlt (Rgb, &Color);
  Status = Gop->Blt (Gop, &Color, EfiBltVideoFill, 0, 0, DestX, DestY, Width, Height, 0);
  if (EFI_ERROR (Status)) {
    UEFI_GUI_LOG_ERROR ("GOP Blt(VideoFill)", Status);
  }

  return Status;
}

/**
  Read a validated rectangle with GOP Blt(VideoToBltBuffer).
**/
EFI_STATUS
UefiGuiBltRead (
  OUT UINT32  *Pixels,
  IN  UINTN   SourceX,
  IN  UINTN   SourceY,
  IN  UINTN   Width,
  IN  UINTN   Height
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *In;
  EFI_STATUS                     Status;
  UINTN                          RowsPerChunk;
  UINTN                          Done;
  UINTN                          Rows;
  UINTN                          Index;
  UINTN                          Count;

  Gop          = gUefiGuiDisplay.Gop;
  RowsPerChunk = gUefiGuiDisplay.BltCapacity / Width;

  for (Done = 0; Done < Height; Done += Rows) {
    Rows   = MIN (RowsPerChunk, Height - Done);
    Status = Gop->Blt (
                    Gop,
                    gUefiGuiDisplay.BltBuffer,
                    EfiBltVideoToBltBuffer,
                    SourceX,
                    SourceY + Done,
                    0,
                    0,
                    Width,
                    Rows,
                    Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                    );
    if (EFI_ERROR (Status)) {
      UEFI_GUI_LOG_ERROR ("GOP Blt(VideoToBltBuffer)", Status);
      return Status;
    }

    In    = gUefiGuiDisplay.BltBuffer;
    Count = Rows * Width;
    for (Index = 0; Index < Count; Index++) {
      Pixels[Done * Width + Index] = ((UINT32)In[Index].Red << 16) |
                                     ((UINT32)In[Index].Green << 8) |
                                     (UINT32)In[Index].Blue;
    }
  }

  return EFI_SUCCESS;
}

/**
  Release the conversion buffer.
**/
VOID
UefiGuiBltShutdown (
  VOID
  )
{
  UefiGuiFree (gUefiGuiDisplay.BltBuffer);
  gUefiGuiDisplay.BltBuffer   = NULL;
  gUefiGuiDisplay.BltCapacity = 0;
}
