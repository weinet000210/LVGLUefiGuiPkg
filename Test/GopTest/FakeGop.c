/** @file
  A GOP instance with a synthetic mode, for testing PixelBltOnly and the
  frame buffer backend with formats the test machine does not have.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GopTest.h"

STATIC
EFI_STATUS
EFIAPI
FakeGopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  FAKE_GOP  *Fake;

  Fake = BASE_CR (This, FAKE_GOP, Gop);
  if (ModeNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocateCopyPool (sizeof (Fake->Info), &Fake->Info);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (Fake->Info);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FakeGopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  return (ModeNumber == 0) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
FakeGopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta  OPTIONAL
  )
{
  FAKE_GOP  *Fake;

  Fake = BASE_CR (This, FAKE_GOP, Gop);
  if (Fake->Real == NULL) {
    return EFI_DEVICE_ERROR;
  }

  return Fake->Real->Blt (
                       Fake->Real,
                       BltBuffer,
                       BltOperation,
                       SourceX,
                       SourceY,
                       DestinationX,
                       DestinationY,
                       Width,
                       Height,
                       Delta
                       );
}

/**
  Prepare a fake GOP.
**/
VOID
FakeGopInit (
  OUT FAKE_GOP                      *Fake,
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Real  OPTIONAL,
  IN  UINT32                        Width,
  IN  UINT32                        Height,
  IN  UINT32                        PixelsPerScanLine,
  IN  EFI_GRAPHICS_PIXEL_FORMAT     Format,
  IN  CONST EFI_PIXEL_BITMASK       *Mask  OPTIONAL,
  IN  EFI_PHYSICAL_ADDRESS          FrameBufferBase,
  IN  UINTN                         FrameBufferSize
  )
{
  ZeroMem (Fake, sizeof (*Fake));
  Fake->Gop.QueryMode              = FakeGopQueryMode;
  Fake->Gop.SetMode                = FakeGopSetMode;
  Fake->Gop.Blt                    = FakeGopBlt;
  Fake->Gop.Mode                   = &Fake->Mode;
  Fake->Real                       = Real;
  Fake->Mode.MaxMode               = 1;
  Fake->Mode.Mode                  = 0;
  Fake->Mode.Info                  = &Fake->Info;
  Fake->Mode.SizeOfInfo            = sizeof (Fake->Info);
  Fake->Mode.FrameBufferBase       = FrameBufferBase;
  Fake->Mode.FrameBufferSize       = FrameBufferSize;
  Fake->Info.Version               = 0;
  Fake->Info.HorizontalResolution  = Width;
  Fake->Info.VerticalResolution    = Height;
  Fake->Info.PixelsPerScanLine     = PixelsPerScanLine;
  Fake->Info.PixelFormat           = Format;
  if (Mask != NULL) {
    CopyMem (&Fake->Info.PixelInformation, Mask, sizeof (*Mask));
  }
}
