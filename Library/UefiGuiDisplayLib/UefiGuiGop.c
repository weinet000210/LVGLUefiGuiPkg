/** @file
  GOP discovery, mode selection, validation and the public entry points of
  UefiGuiDisplayLib.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiDisplayInternal.h"

UEFI_GUI_DISPLAY_CONTEXT  gUefiGuiDisplay;

/**
  Find the GOP to drive: the one on the console output handle (ConSplitter,
  reaches every active display) if present, else the first one installed.
**/
STATIC
EFI_STATUS
UefiGuiFindGop (
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL  **Gop
  )
{
  EFI_STATUS  Status;

  *Gop = NULL;
  if (gST->ConsoleOutHandle != NULL) {
    Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)Gop);
    if (!EFI_ERROR (Status) && (*Gop != NULL)) {
      return EFI_SUCCESS;
    }
  }

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)Gop);
  if (EFI_ERROR (Status) || (*Gop == NULL)) {
    *Gop = NULL;
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

/**
  Switch to the largest mode that fits PreferredWidth x PreferredHeight.
  Keeps the current mode when it already is that mode or when no mode fits.
**/
STATIC
EFI_STATUS
UefiGuiSelectMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                        PreferredWidth,
  IN UINT32                        PreferredHeight
  )
{
  EFI_STATUS                            Status;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  UINTN                                 InfoSize;
  UINT32                                Mode;
  UINT32                                BestMode;
  UINT64                                BestArea;
  UINT64                                Area;

  BestMode = Gop->Mode->Mode;
  BestArea = 0;
  for (Mode = 0; Mode < Gop->Mode->MaxMode; Mode++) {
    Status = Gop->QueryMode (Gop, Mode, &InfoSize, &Info);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if ((Info->HorizontalResolution <= PreferredWidth) &&
        (Info->VerticalResolution <= PreferredHeight) &&
        (Info->HorizontalResolution > 0) &&
        (Info->VerticalResolution > 0))
    {
      Area = (UINT64)Info->HorizontalResolution * Info->VerticalResolution;
      if (Area > BestArea) {
        BestArea = Area;
        BestMode = Mode;
      }
    }

    gBS->FreePool (Info);
  }

  if (BestArea == 0) {
    DEBUG ((
      DEBUG_WARN,
      "[UefiGui] no GOP mode fits %ux%u, keeping mode %u\n",
      PreferredWidth,
      PreferredHeight,
      Gop->Mode->Mode
      ));
    return EFI_SUCCESS;
  }

  if (BestMode == Gop->Mode->Mode) {
    return EFI_SUCCESS;
  }

  Status = Gop->SetMode (Gop, BestMode);
  if (EFI_ERROR (Status)) {
    UEFI_GUI_LOG_ERROR ("GOP SetMode", Status);
    return Status;
  }

  gUefiGuiDisplay.ModeChanged = TRUE;
  return EFI_SUCCESS;
}

/**
  Capture and validate the active mode.
**/
STATIC
EFI_STATUS
UefiGuiReadMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     *Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  UEFI_GUI_DISPLAY_INFO                 *Info;

  Mode = Gop->Mode;
  if ((Mode == NULL) || (Mode->Info == NULL)) {
    return EFI_UNSUPPORTED;
  }

  ModeInfo = Mode->Info;
  if ((ModeInfo->HorizontalResolution == 0) ||
      (ModeInfo->VerticalResolution == 0) ||
      (ModeInfo->PixelFormat >= PixelFormatMax))
  {
    DEBUG ((
      DEBUG_ERROR,
      "[UefiGui] GOP mode %u invalid: %ux%u format %d\n",
      Mode->Mode,
      ModeInfo->HorizontalResolution,
      ModeInfo->VerticalResolution,
      (INT32)ModeInfo->PixelFormat
      ));
    return EFI_UNSUPPORTED;
  }

  Info                       = &gUefiGuiDisplay.Info;
  Info->ModeNumber           = Mode->Mode;
  Info->MaxMode              = Mode->MaxMode;
  Info->HorizontalResolution = ModeInfo->HorizontalResolution;
  Info->VerticalResolution   = ModeInfo->VerticalResolution;
  Info->PixelsPerScanLine    = ModeInfo->PixelsPerScanLine;
  Info->PixelFormat          = ModeInfo->PixelFormat;
  CopyMem (&Info->PixelInformation, &ModeInfo->PixelInformation, sizeof (Info->PixelInformation));
  if (ModeInfo->PixelFormat == PixelBltOnly) {
    //
    // The specification says these fields are not valid in this mode.
    //
    Info->FrameBufferBase = 0;
    Info->FrameBufferSize = 0;
  } else {
    Info->FrameBufferBase = Mode->FrameBufferBase;
    Info->FrameBufferSize = Mode->FrameBufferSize;
  }

  return EFI_SUCCESS;
}

/**
  Same as UefiGuiDisplayInitialize() but drives the given GOP instance.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayInitializeWithGop (
  IN CONST UEFI_GUI_DISPLAY_CONFIG  *Config  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop
  )
{
  EFI_STATUS                Status;
  UEFI_GUI_DISPLAY_BACKEND  Backend;
  UINT32                    DrawLines;

  if (gUefiGuiDisplay.Initialized) {
    return EFI_ALREADY_STARTED;
  }

  if ((Gop == NULL) || (Gop->Mode == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Backend   = UefiGuiDisplayBackendAuto;
  DrawLines = UEFI_GUI_DEFAULT_DRAW_LINES;
  if (Config != NULL) {
    if ((UINT32)Config->Backend >= (UINT32)UefiGuiDisplayBackendMax) {
      return EFI_INVALID_PARAMETER;
    }

    if (Config->DrawBufferLines > UEFI_GUI_MAX_DRAW_LINES) {
      return EFI_INVALID_PARAMETER;
    }

    Backend = Config->Backend;
    if (Config->DrawBufferLines != 0) {
      DrawLines = Config->DrawBufferLines;
    }
  }

  ZeroMem (&gUefiGuiDisplay, sizeof (gUefiGuiDisplay));
  gUefiGuiDisplay.Gop          = Gop;
  gUefiGuiDisplay.OriginalMode = Gop->Mode->Mode;

  if ((Config != NULL) && (Config->PreferredWidth != 0) && (Config->PreferredHeight != 0)) {
    Status = UefiGuiSelectMode (Gop, Config->PreferredWidth, Config->PreferredHeight);
    if (EFI_ERROR (Status)) {
      goto Fail;
    }
  }

  Status = UefiGuiReadMode (Gop);
  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  //
  // The Blt conversion buffer is always allocated: the frame buffer backend
  // has no use for it today, but it keeps ReadRect and a later fallback
  // simple and costs one band of pixels.
  //
  Status = UefiGuiBltInitialize (DrawLines);
  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  if (Backend == UefiGuiDisplayBackendFrameBuffer) {
    Status = UefiGuiFrameBufferInitialize ();
    if (EFI_ERROR (Status)) {
      goto Fail;
    }

    gUefiGuiDisplay.Info.ActiveBackend = UefiGuiDisplayBackendFrameBuffer;
  } else {
    //
    // Auto never picks the frame buffer: its safety cannot be proven for
    // every platform, and Blt works everywhere.
    //
    gUefiGuiDisplay.Info.ActiveBackend = UefiGuiDisplayBackendBlt;
  }

  gUefiGuiDisplay.Initialized = TRUE;
  DEBUG ((
    DEBUG_INFO,
    "[UefiGui] display %ux%u mode %u format %d backend %d\n",
    gUefiGuiDisplay.Info.HorizontalResolution,
    gUefiGuiDisplay.Info.VerticalResolution,
    gUefiGuiDisplay.Info.ModeNumber,
    (INT32)gUefiGuiDisplay.Info.PixelFormat,
    (INT32)gUefiGuiDisplay.Info.ActiveBackend
    ));
  return EFI_SUCCESS;

Fail:
  UefiGuiBltShutdown ();
  if (gUefiGuiDisplay.ModeChanged) {
    Gop->SetMode (Gop, gUefiGuiDisplay.OriginalMode);
  }

  ZeroMem (&gUefiGuiDisplay, sizeof (gUefiGuiDisplay));
  return Status;
}

/**
  Locate GOP, optionally switch mode, validate the mode and allocate the
  conversion buffer.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayInitialize (
  IN CONST UEFI_GUI_DISPLAY_CONFIG  *Config  OPTIONAL
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  if (gUefiGuiDisplay.Initialized) {
    return EFI_ALREADY_STARTED;
  }

  Status = UefiGuiFindGop (&Gop);
  if (EFI_ERROR (Status)) {
    UEFI_GUI_LOG_ERROR ("locate GOP", Status);
    return Status;
  }

  return UefiGuiDisplayInitializeWithGop (Config, Gop);
}

/**
  Copy the active mode description.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayGetInfo (
  OUT UEFI_GUI_DISPLAY_INFO  *Info
  )
{
  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!gUefiGuiDisplay.Initialized) {
    return EFI_NOT_STARTED;
  }

  CopyMem (Info, &gUefiGuiDisplay.Info, sizeof (*Info));
  return EFI_SUCCESS;
}

/**
  Convert and draw a rectangle.
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
  )
{
  UINTN  BytesPerPixel;

  if (!gUefiGuiDisplay.Initialized) {
    return EFI_NOT_STARTED;
  }

  BytesPerPixel = UefiGuiSourceBytesPerPixel (Format);
  if ((Source == NULL) || (BytesPerPixel == 0) || !UefiGuiRectIsInside (DestX, DestY, Width, Height)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Width is bounded by the display width here, so this cannot overflow.
  //
  if (SourceStrideBytes < Width * BytesPerPixel) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Height > 1) && (SourceStrideBytes > (MAX_UINTN - (UINTN)Source) / (Height - 1))) {
    return EFI_INVALID_PARAMETER;
  }

  if (gUefiGuiDisplay.Info.ActiveBackend == UefiGuiDisplayBackendFrameBuffer) {
    return UefiGuiFrameBufferFlush (Source, Format, SourceStrideBytes, DestX, DestY, Width, Height);
  }

  return UefiGuiBltFlush (Source, Format, SourceStrideBytes, DestX, DestY, Width, Height);
}

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
  )
{
  if (!gUefiGuiDisplay.Initialized) {
    return EFI_NOT_STARTED;
  }

  if (!UefiGuiRectIsInside (DestX, DestY, Width, Height)) {
    return EFI_INVALID_PARAMETER;
  }

  if (gUefiGuiDisplay.Info.ActiveBackend == UefiGuiDisplayBackendFrameBuffer) {
    return UefiGuiFrameBufferFill (Rgb & 0xFFFFFF, DestX, DestY, Width, Height);
  }

  return UefiGuiBltFill (Rgb & 0xFFFFFF, DestX, DestY, Width, Height);
}

/**
  Read back a rectangle of the screen as 0x00RRGGBB values.
**/
EFI_STATUS
EFIAPI
UefiGuiDisplayReadRect (
  OUT UINT32  *Pixels,
  IN  UINTN   SourceX,
  IN  UINTN   SourceY,
  IN  UINTN   Width,
  IN  UINTN   Height
  )
{
  if (!gUefiGuiDisplay.Initialized) {
    return EFI_NOT_STARTED;
  }

  if ((Pixels == NULL) || !UefiGuiRectIsInside (SourceX, SourceY, Width, Height)) {
    return EFI_INVALID_PARAMETER;
  }

  if (gUefiGuiDisplay.Info.ActiveBackend == UefiGuiDisplayBackendFrameBuffer) {
    return UefiGuiFrameBufferRead (Pixels, SourceX, SourceY, Width, Height);
  }

  return UefiGuiBltRead (Pixels, SourceX, SourceY, Width, Height);
}

/**
  Release buffers and restore the original GOP mode if it was changed.
**/
VOID
EFIAPI
UefiGuiDisplayShutdown (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!gUefiGuiDisplay.Initialized) {
    return;
  }

  UefiGuiBltShutdown ();
  if (gUefiGuiDisplay.ModeChanged) {
    Status = gUefiGuiDisplay.Gop->SetMode (gUefiGuiDisplay.Gop, gUefiGuiDisplay.OriginalMode);
    if (EFI_ERROR (Status)) {
      UEFI_GUI_LOG_ERROR ("GOP SetMode (restore)", Status);
    }
  }

  ZeroMem (&gUefiGuiDisplay, sizeof (gUefiGuiDisplay));
}
