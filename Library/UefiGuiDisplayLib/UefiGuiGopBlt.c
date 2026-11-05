/** @file
  Blt display backend: convert into EFI_GRAPHICS_OUTPUT_BLT_PIXEL rows and
  copy them with GOP Blt(). Works with every GOP pixel format, including
  PixelBltOnly, because the firmware converts Blt pixels to the device format.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiDisplayInternal.h"

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
