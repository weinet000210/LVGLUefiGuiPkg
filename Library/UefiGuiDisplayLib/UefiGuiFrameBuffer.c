/** @file
  Optional direct frame buffer backend.

  Only used when the application asks for UefiGuiDisplayBackendFrameBuffer.
  The frame buffer is validated before the first write; any failed check
  makes initialization return EFI_UNSUPPORTED instead of guessing.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiDisplayInternal.h"

/**
  Address of pixel (X, Y).
**/
STATIC
UINT8 *
UefiGuiFbPixelAddress (
  IN UINTN  X,
  IN UINTN  Y
  )
{
  return gUefiGuiDisplay.FrameBuffer +
         Y * gUefiGuiDisplay.FrameBufferStride +
         X * gUefiGuiDisplay.Layout.BytesPerPixel;
}

/**
  Store one native pixel.
**/
STATIC
VOID
UefiGuiFbWrite (
  IN UINT8   *Address,
  IN UINT32  Pixel
  )
{
  if (gUefiGuiDisplay.Layout.BytesPerPixel == 4) {
    *(volatile UINT32 *)Address = Pixel;
  } else {
    *(volatile UINT16 *)Address = (UINT16)Pixel;
  }
}

/**
  Load one native pixel.
**/
STATIC
UINT32
UefiGuiFbRead (
  IN CONST UINT8  *Address
  )
{
  if (gUefiGuiDisplay.Layout.BytesPerPixel == 4) {
    return *(volatile CONST UINT32 *)Address;
  }

  return *(volatile CONST UINT16 *)Address;
}

/**
  Check that the GCD memory space map covers [Base, Base + Length) with
  present memory that is neither read-only nor read-protected.
**/
STATIC
EFI_STATUS
UefiGuiCheckMemoryAttributes (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT64                Length
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Descriptor;
  EFI_PHYSICAL_ADDRESS             Address;
  EFI_PHYSICAL_ADDRESS             End;
  EFI_STATUS                       Status;

  if (gDS == NULL) {
    return EFI_UNSUPPORTED;
  }

  End = Base + Length;
  for (Address = Base; Address < End; Address = Descriptor.BaseAddress + Descriptor.Length) {
    Status = gDS->GetMemorySpaceDescriptor (Address, &Descriptor);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "[UefiGui] frame buffer 0x%lx: no GCD descriptor (%r)\n", Address, Status));
      return EFI_UNSUPPORTED;
    }

    if ((Descriptor.GcdMemoryType == EfiGcdMemoryTypeNonExistent) ||
        ((Descriptor.Attributes & (EFI_MEMORY_RO | EFI_MEMORY_RP)) != 0) ||
        (Descriptor.Length == 0))
    {
      DEBUG ((
        DEBUG_ERROR,
        "[UefiGui] frame buffer 0x%lx: GCD type %d attributes 0x%lx not writable\n",
        Address,
        (INT32)Descriptor.GcdMemoryType,
        Descriptor.Attributes
        ));
      return EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}

/**
  Validate the frame buffer of the active mode and prepare the backend.

  @retval EFI_SUCCESS       Frame buffer usable.
  @retval EFI_UNSUPPORTED   A check failed; details go to DEBUG_ERROR.
**/
EFI_STATUS
UefiGuiFrameBufferInitialize (
  VOID
  )
{
  UEFI_GUI_DISPLAY_INFO  *Info;
  EFI_STATUS             Status;
  UINT64                 Stride;
  UINT64                 Needed;
  UINT8                  *Probe;
  UINT32                 Saved;
  UINT32                 Pattern;
  UINT32                 ColorMask;
  UINTN                  Index;
  STATIC CONST UINT32    ProbeColors[] = { 0x5AA53C, 0xA55AC3 };

  Info   = &gUefiGuiDisplay.Info;
  Status = UefiGuiGetPixelLayout (Info->PixelFormat, &Info->PixelInformation, &gUefiGuiDisplay.Layout);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[UefiGui] frame buffer: pixel format %d not writable\n", (INT32)Info->PixelFormat));
    return EFI_UNSUPPORTED;
  }

  if ((Info->FrameBufferBase == 0) || (Info->PixelsPerScanLine < Info->HorizontalResolution)) {
    DEBUG ((
      DEBUG_ERROR,
      "[UefiGui] frame buffer: base 0x%lx, stride %u, width %u invalid\n",
      Info->FrameBufferBase,
      Info->PixelsPerScanLine,
      Info->HorizontalResolution
      ));
    return EFI_UNSUPPORTED;
  }

  Stride = (UINT64)Info->PixelsPerScanLine * gUefiGuiDisplay.Layout.BytesPerPixel;
  Needed = Stride * Info->VerticalResolution;
  if ((Needed > (UINT64)Info->FrameBufferSize) || (Info->FrameBufferBase > MAX_ADDRESS - Needed)) {
    DEBUG ((
      DEBUG_ERROR,
      "[UefiGui] frame buffer: size 0x%lx smaller than stride x height 0x%lx\n",
      (UINT64)Info->FrameBufferSize,
      Needed
      ));
    return EFI_UNSUPPORTED;
  }

  Status = UefiGuiCheckMemoryAttributes (Info->FrameBufferBase, Needed);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gUefiGuiDisplay.FrameBuffer       = (UINT8 *)(UINTN)Info->FrameBufferBase;
  gUefiGuiDisplay.FrameBufferStride = (UINTN)Stride;

  //
  // Write/read-back probe on the first pixel, then restore it.
  //
  ColorMask = UefiGuiEncodeWithLayout (0xFFFFFF, &gUefiGuiDisplay.Layout);
  Probe     = UefiGuiFbPixelAddress (0, 0);
  Saved     = UefiGuiFbRead (Probe);
  for (Index = 0; Index < ARRAY_SIZE (ProbeColors); Index++) {
    Pattern = UefiGuiEncodeWithLayout (ProbeColors[Index], &gUefiGuiDisplay.Layout);
    UefiGuiFbWrite (Probe, Pattern);
    if ((UefiGuiFbRead (Probe) & ColorMask) != Pattern) {
      UefiGuiFbWrite (Probe, Saved);
      DEBUG ((DEBUG_ERROR, "[UefiGui] frame buffer: write/read-back probe failed\n"));
      gUefiGuiDisplay.FrameBuffer = NULL;
      return EFI_UNSUPPORTED;
    }
  }

  UefiGuiFbWrite (Probe, Saved);
  return EFI_SUCCESS;
}

/**
  Convert and write a validated rectangle to the frame buffer.
**/
EFI_STATUS
UefiGuiFrameBufferFlush (
  IN CONST UINT8             *Source,
  IN UEFI_GUI_SOURCE_FORMAT  Format,
  IN UINTN                   SourceStrideBytes,
  IN UINTN                   DestX,
  IN UINTN                   DestY,
  IN UINTN                   Width,
  IN UINTN                   Height
  )
{
  UINTN        Row;
  UINTN        Column;
  UINT8        *Out;
  CONST UINT8                  *SourceRow;
  UINT32                       BytesPerPixel;
  CONST UEFI_GUI_PIXEL_LAYOUT  *Layout;
  BOOLEAN                      Bgr;
  BOOLEAN                      Rgb;
  volatile UINT32              *Out32;
  CONST UINT32                 *Source32;
  BOOLEAN                      Aligned;
  UINT32                       Word;

  BytesPerPixel = gUefiGuiDisplay.Layout.BytesPerPixel;
  Layout        = &gUefiGuiDisplay.Layout;
  //
  // Fast paths for an XRGB8888 source on 32-bit frame buffers whose
  // channels are whole bytes (PixelBlueGreenRedReserved8BitPerColor, or
  // PixelRedGreenBlueReserved8BitPerColor / a matching PixelBitMask). The
  // layout comes from the GOP mode; nothing is assumed about the device.
  //
  Bgr = (BOOLEAN)((Format == UefiGuiSourceFormatXrgb8888) && (BytesPerPixel == 4) &&
                  (Layout->Red.Shift == 16) && (Layout->Red.Width == 8) &&
                  (Layout->Green.Shift == 8) && (Layout->Green.Width == 8) &&
                  (Layout->Blue.Shift == 0) && (Layout->Blue.Width == 8));
  Rgb = (BOOLEAN)((Format == UefiGuiSourceFormatXrgb8888) && (BytesPerPixel == 4) &&
                  (Layout->Red.Shift == 0) && (Layout->Red.Width == 8) &&
                  (Layout->Green.Shift == 8) && (Layout->Green.Width == 8) &&
                  (Layout->Blue.Shift == 16) && (Layout->Blue.Width == 8));
  for (Row = 0; Row < Height; Row++) {
    SourceRow = Source + Row * SourceStrideBytes;
    Out       = UefiGuiFbPixelAddress (DestX, DestY + Row);
    if (Bgr || Rgb) {
      Out32    = (volatile UINT32 *)Out;
      Source32 = (CONST UINT32 *)SourceRow;
      Aligned  = (BOOLEAN)(((UINTN)SourceRow & 3) == 0);
      for (Column = 0; Column < Width; Column++) {
        Word = Aligned ? Source32[Column] : ReadUnaligned32 (Source32 + Column);
        Word &= 0x00FFFFFF;
        if (Rgb) {
          Word = ((Word & 0xFF) << 16) | (Word & 0xFF00) | ((Word >> 16) & 0xFF);
        }

        Out32[Column] = Word;
      }

      continue;
    }

    for (Column = 0; Column < Width; Column++) {
      UefiGuiFbWrite (
        Out,
        UefiGuiEncodeWithLayout (UefiGuiSourcePixelToRgb (SourceRow, Format, Column), &gUefiGuiDisplay.Layout)
        );
      Out += BytesPerPixel;
    }
  }

  return EFI_SUCCESS;
}

/**
  Fill a validated rectangle in the frame buffer.
**/
EFI_STATUS
UefiGuiFrameBufferFill (
  IN UINT32  Rgb,
  IN UINTN   DestX,
  IN UINTN   DestY,
  IN UINTN   Width,
  IN UINTN   Height
  )
{
  UINTN   Row;
  UINTN   Column;
  UINT8   *Out;
  UINT32  Pixel;

  Pixel = UefiGuiEncodeWithLayout (Rgb, &gUefiGuiDisplay.Layout);
  for (Row = 0; Row < Height; Row++) {
    Out = UefiGuiFbPixelAddress (DestX, DestY + Row);
    for (Column = 0; Column < Width; Column++) {
      UefiGuiFbWrite (Out, Pixel);
      Out += gUefiGuiDisplay.Layout.BytesPerPixel;
    }
  }

  return EFI_SUCCESS;
}

/**
  Read and decode a validated rectangle from the frame buffer.
**/
EFI_STATUS
UefiGuiFrameBufferRead (
  OUT UINT32  *Pixels,
  IN  UINTN   SourceX,
  IN  UINTN   SourceY,
  IN  UINTN   Width,
  IN  UINTN   Height
  )
{
  UINTN        Row;
  UINTN        Column;
  CONST UINT8  *In;

  for (Row = 0; Row < Height; Row++) {
    In = UefiGuiFbPixelAddress (SourceX, SourceY + Row);
    for (Column = 0; Column < Width; Column++) {
      *Pixels++ = UefiGuiDecodeWithLayout (UefiGuiFbRead (In), &gUefiGuiDisplay.Layout);
      In       += gUefiGuiDisplay.Layout.BytesPerPixel;
    }
  }

  return EFI_SUCCESS;
}
