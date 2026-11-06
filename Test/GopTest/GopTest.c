/** @file
  GopTest - Phase 1 test for UefiGuiDisplayLib.

  Checks, in order:
    1. Pixel encoding for RGB8, BGR8, PixelBitMask (10:10:10 and 5:6:5) and
       rejection of invalid masks and PixelBltOnly.
    2. Initialization, info, and argument validation of the real display.
    3. Mode selection and restoration of the original mode.
    4. PixelBltOnly: a fake GOP that reports PixelBltOnly and forwards Blt()
       to the real one; the frame buffer backend must refuse it.
    5. Frame buffer backend against frame buffers in plain memory, in every
       writable format, including stride padding and bounds.
    6. Visual test screen with the Blt backend, verified by read-back.

  The last line is "UEFIGUI-GOPTEST: PASS" or "UEFIGUI-GOPTEST: FAIL".

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GopTest.h"

#define HOLD_SECONDS  6

STATIC UINTN  mChecks;
STATIC UINTN  mFailures;

STATIC CONST EFI_PIXEL_BITMASK  mMask101010 = { 0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000 };
STATIC CONST EFI_PIXEL_BITMASK  mMask565    = { 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 };

/**
  Record and print one check result.
**/
VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed
  )
{
  mChecks++;
  if (!Passed) {
    mFailures++;
  }

  Print (L"  [%s] %s\n", Passed ? L"PASS" : L"FAIL", Name);
}

/**
  Find the GOP the display library drives by default: the one on the
  console output handle, else the first one (the UEFI Shell replaces the
  console output handle with its own, which has no GOP).
**/
STATIC
EFI_STATUS
FindRealGop (
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL  **Gop
  )
{
  if (!EFI_ERROR (gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)Gop))) {
    return EFI_SUCCESS;
  }

  return gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)Gop);
}

/**
  1. Pixel encoding.
**/
STATIC
VOID
TestEncoding (
  VOID
  )
{
  EFI_PIXEL_BITMASK  Bad;

  Print (L"Pixel encoding:\n");
  Check (L"RGB8 0x112233 -> 0x00332211", (BOOLEAN)(UefiGuiDisplayEncodePixel (0x112233, PixelRedGreenBlueReserved8BitPerColor, NULL) == 0x00332211));
  Check (L"BGR8 0x112233 -> 0x00112233", (BOOLEAN)(UefiGuiDisplayEncodePixel (0x112233, PixelBlueGreenRedReserved8BitPerColor, NULL) == 0x00112233));
  Check (L"BitMask 10:10:10 white", (BOOLEAN)(UefiGuiDisplayEncodePixel (0xFFFFFF, PixelBitMask, &mMask101010) == 0x3FFFFFFF));
  Check (L"BitMask 10:10:10 red", (BOOLEAN)(UefiGuiDisplayEncodePixel (0xFF0000, PixelBitMask, &mMask101010) == 0x3FF00000));
  Check (
    L"BitMask 10:10:10 0x808080 scales to 514",
    (BOOLEAN)(UefiGuiDisplayEncodePixel (0x808080, PixelBitMask, &mMask101010) == ((514u << 20) | (514u << 10) | 514u))
    );
  Check (L"BitMask 5:6:5 white", (BOOLEAN)(UefiGuiDisplayEncodePixel (0xFFFFFF, PixelBitMask, &mMask565) == 0xFFFF));
  Check (L"BitMask 5:6:5 green", (BOOLEAN)(UefiGuiDisplayEncodePixel (0x00FF00, PixelBitMask, &mMask565) == 0x07E0));
  Check (L"BitMask 5:6:5 blue", (BOOLEAN)(UefiGuiDisplayEncodePixel (0x0000FF, PixelBitMask, &mMask565) == 0x001F));

  CopyMem (&Bad, &mMask101010, sizeof (Bad));
  Bad.RedMask = 0x00FF00FF;
  Check (L"non-contiguous mask rejected", (BOOLEAN)(UefiGuiDisplayEncodePixel (0xFFFFFF, PixelBitMask, &Bad) == 0));
  CopyMem (&Bad, &mMask101010, sizeof (Bad));
  Bad.GreenMask = 0x3FF00000;
  Check (L"overlapping masks rejected", (BOOLEAN)(UefiGuiDisplayEncodePixel (0xFFFFFF, PixelBitMask, &Bad) == 0));
  Check (L"BitMask without masks rejected", (BOOLEAN)(UefiGuiDisplayEncodePixel (0xFFFFFF, PixelBitMask, NULL) == 0));
  Check (L"PixelBltOnly has no native encoding", (BOOLEAN)(UefiGuiDisplayEncodePixel (0xFFFFFF, PixelBltOnly, NULL) == 0));
}

/**
  2. Initialization, info and argument validation on the real display.
**/
STATIC
VOID
TestRealDisplay (
  VOID
  )
{
  UEFI_GUI_DISPLAY_INFO    Info;
  UEFI_GUI_DISPLAY_CONFIG  Config;
  UINT32                   Pixel;

  UINTN                    W;
  UINTN                    H;
  STATIC CONST CHAR16      *FormatNames[] = { L"RGB8", L"BGR8", L"BitMask", L"BltOnly" };

  Pixel = 0;
  Print (L"Real display:\n");
  Check (L"GetInfo before init -> EFI_NOT_STARTED", (BOOLEAN)(UefiGuiDisplayGetInfo (&Info) == EFI_NOT_STARTED));
  Check (L"Flush before init -> EFI_NOT_STARTED", (BOOLEAN)(UefiGuiDisplayFillRect (0, 0, 0, 1, 1) == EFI_NOT_STARTED));

  ZeroMem (&Config, sizeof (Config));
  Config.Backend = UefiGuiDisplayBackendMax;
  Check (L"invalid backend rejected", (BOOLEAN)(UefiGuiDisplayInitialize (&Config) == EFI_INVALID_PARAMETER));
  Config.Backend         = UefiGuiDisplayBackendAuto;
  Config.DrawBufferLines = 100000;
  Check (L"invalid DrawBufferLines rejected", (BOOLEAN)(UefiGuiDisplayInitialize (&Config) == EFI_INVALID_PARAMETER));

  Check (L"Initialize(NULL) (defaults)", (BOOLEAN)(UefiGuiDisplayInitialize (NULL) == EFI_SUCCESS));
  Check (L"second Initialize -> EFI_ALREADY_STARTED", (BOOLEAN)(UefiGuiDisplayInitialize (NULL) == EFI_ALREADY_STARTED));
  Check (L"GetInfo(NULL) -> EFI_INVALID_PARAMETER", (BOOLEAN)(UefiGuiDisplayGetInfo (NULL) == EFI_INVALID_PARAMETER));
  if (EFI_ERROR (UefiGuiDisplayGetInfo (&Info))) {
    Check (L"GetInfo", FALSE);
    return;
  }

  Print (
    L"  mode %u/%u %ux%u stride %u %s fb 0x%lx size 0x%lx\n",
    Info.ModeNumber,
    Info.MaxMode,
    Info.HorizontalResolution,
    Info.VerticalResolution,
    Info.PixelsPerScanLine,
    ((UINTN)Info.PixelFormat < ARRAY_SIZE (FormatNames)) ? FormatNames[Info.PixelFormat] : L"?",
    Info.FrameBufferBase,
    (UINT64)Info.FrameBufferSize
    );
  Check (L"Auto backend resolves to Blt", (BOOLEAN)(Info.ActiveBackend == UefiGuiDisplayBackendBlt));

  W = Info.HorizontalResolution;
  H = Info.VerticalResolution;
  Check (L"Flush NULL source rejected", (BOOLEAN)(UefiGuiDisplayFlushRect (NULL, UefiGuiSourceFormatXrgb8888, 4, 0, 0, 1, 1) == EFI_INVALID_PARAMETER));
  Check (L"zero width rejected", (BOOLEAN)(UefiGuiDisplayFlushRect (&Pixel, UefiGuiSourceFormatXrgb8888, 4, 0, 0, 0, 1) == EFI_INVALID_PARAMETER));
  Check (L"zero height rejected", (BOOLEAN)(UefiGuiDisplayFlushRect (&Pixel, UefiGuiSourceFormatXrgb8888, 4, 0, 0, 1, 0) == EFI_INVALID_PARAMETER));
  Check (L"bad source format rejected", (BOOLEAN)(UefiGuiDisplayFlushRect (&Pixel, UefiGuiSourceFormatMax, 4, 0, 0, 1, 1) == EFI_INVALID_PARAMETER));
  Check (L"stride smaller than a row rejected", (BOOLEAN)(UefiGuiDisplayFlushRect (&Pixel, UefiGuiSourceFormatXrgb8888, 4, 0, 0, 2, 1) == EFI_INVALID_PARAMETER));
  Check (L"rect one pixel past right edge rejected", (BOOLEAN)(UefiGuiDisplayFillRect (0, W - 1, 0, 2, 1) == EFI_INVALID_PARAMETER));
  Check (L"rect one pixel past bottom edge rejected", (BOOLEAN)(UefiGuiDisplayFillRect (0, 0, H - 1, 1, 2) == EFI_INVALID_PARAMETER));
  Check (L"X = width rejected", (BOOLEAN)(UefiGuiDisplayFillRect (0, W, 0, 1, 1) == EFI_INVALID_PARAMETER));
  Check (L"X + Width overflow rejected", (BOOLEAN)(UefiGuiDisplayFillRect (0, 1, 0, MAX_UINTN, 1) == EFI_INVALID_PARAMETER));
  Check (L"huge X rejected", (BOOLEAN)(UefiGuiDisplayFillRect (0, MAX_UINTN, 0, 1, 1) == EFI_INVALID_PARAMETER));
  Check (L"read outside display rejected", (BOOLEAN)(UefiGuiDisplayReadRect (&Pixel, W, H, 1, 1) == EFI_INVALID_PARAMETER));
  Check (L"last pixel is writable", (BOOLEAN)(UefiGuiDisplayFillRect (0x123456, W - 1, H - 1, 1, 1) == EFI_SUCCESS));
  Check (
    L"last pixel reads back",
    (BOOLEAN)((UefiGuiDisplayReadRect (&Pixel, W - 1, H - 1, 1, 1) == EFI_SUCCESS) && (Pixel == 0x123456))
    );

  UefiGuiDisplayShutdown ();
  Check (L"Shutdown leaves display uninitialized", (BOOLEAN)(UefiGuiDisplayGetInfo (&Info) == EFI_NOT_STARTED));
  UefiGuiDisplayShutdown ();
  Check (L"second Shutdown is harmless", TRUE);
}

/**
  3. Mode selection and restore.
**/
STATIC
VOID
TestModeSwitch (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UEFI_GUI_DISPLAY_CONFIG       Config;
  UEFI_GUI_DISPLAY_INFO         Info;
  UINT32                        Original;

  Print (L"Mode selection:\n");
  if (EFI_ERROR (FindRealGop (&Gop))) {
    Check (L"GOP present", FALSE);
    return;
  }

  Original = Gop->Mode->Mode;
  ZeroMem (&Config, sizeof (Config));
  Config.PreferredWidth  = 800;
  Config.PreferredHeight = 600;
  Check (L"Initialize with preferred 800x600", (BOOLEAN)(UefiGuiDisplayInitialize (&Config) == EFI_SUCCESS));
  if (!EFI_ERROR (UefiGuiDisplayGetInfo (&Info))) {
    Print (L"  selected %ux%u (mode %u, was %u)\n", Info.HorizontalResolution, Info.VerticalResolution, Info.ModeNumber, Original);
    Check (L"selected mode fits 800x600", (BOOLEAN)((Info.HorizontalResolution <= 800) && (Info.VerticalResolution <= 600)));
    Check (L"info matches GOP mode", (BOOLEAN)(Info.ModeNumber == Gop->Mode->Mode));
  }

  UefiGuiDisplayShutdown ();
  Check (L"original mode restored at shutdown", (BOOLEAN)(Gop->Mode->Mode == Original));

  ZeroMem (&Config, sizeof (Config));
  Config.PreferredWidth  = 1;
  Config.PreferredHeight = 1;
  Check (L"no mode fits 1x1: current mode kept", (BOOLEAN)(UefiGuiDisplayInitialize (&Config) == EFI_SUCCESS));
  Check (L"mode unchanged", (BOOLEAN)(Gop->Mode->Mode == Original));
  UefiGuiDisplayShutdown ();
}

/**
  4. PixelBltOnly through a fake GOP that forwards Blt() to the real one.
**/
STATIC
VOID
TestBltOnly (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Real;
  FAKE_GOP                      Fake;
  UEFI_GUI_DISPLAY_CONFIG       Config;
  UEFI_GUI_DISPLAY_INFO         Info;
  UINT32                        Source[4 * 3];
  UINT32                        Back[4 * 3];
  UINTN                         Index;

  Print (L"PixelBltOnly:\n");
  if (EFI_ERROR (FindRealGop (&Real))) {
    Check (L"GOP present", FALSE);
    return;
  }

  FakeGopInit (
    &Fake,
    Real,
    Real->Mode->Info->HorizontalResolution,
    Real->Mode->Info->VerticalResolution,
    0,
    PixelBltOnly,
    NULL,
    0xDEAD0000,   // must be ignored in PixelBltOnly mode
    0x1000
    );

  ZeroMem (&Config, sizeof (Config));
  Config.Backend = UefiGuiDisplayBackendFrameBuffer;
  Check (L"frame buffer backend refused", (BOOLEAN)(UefiGuiDisplayInitializeWithGop (&Config, &Fake.Gop) == EFI_UNSUPPORTED));

  Config.Backend = UefiGuiDisplayBackendAuto;
  Check (L"Auto initializes", (BOOLEAN)(UefiGuiDisplayInitializeWithGop (&Config, &Fake.Gop) == EFI_SUCCESS));
  if (EFI_ERROR (UefiGuiDisplayGetInfo (&Info))) {
    return;
  }

  Check (L"backend is Blt", (BOOLEAN)(Info.ActiveBackend == UefiGuiDisplayBackendBlt));
  Check (L"frame buffer fields ignored", (BOOLEAN)((Info.FrameBufferBase == 0) && (Info.FrameBufferSize == 0)));

  for (Index = 0; Index < ARRAY_SIZE (Source); Index++) {
    Source[Index] = (UINT32)(0x103050 + Index * 0x0A0B0C);
  }

  Check (
    L"FlushRect through Blt()",
    (BOOLEAN)(UefiGuiDisplayFlushRect (Source, UefiGuiSourceFormatXrgb8888, 4 * sizeof (UINT32), 10, 10, 4, 3) == EFI_SUCCESS)
    );
  Check (L"ReadRect through Blt()", (BOOLEAN)(UefiGuiDisplayReadRect (Back, 10, 10, 4, 3) == EFI_SUCCESS));
  Check (L"pixels match", (BOOLEAN)(CompareMem (Source, Back, sizeof (Source)) == 0));
  UefiGuiDisplayShutdown ();
}

/**
  5. Frame buffer backend on a frame buffer in memory.
**/
STATIC
VOID
TestMemoryFrameBuffer (
  IN CONST CHAR16               *Name,
  IN EFI_GRAPHICS_PIXEL_FORMAT  Format,
  IN CONST EFI_PIXEL_BITMASK    *Mask  OPTIONAL,
  IN UINTN                      BytesPerPixel
  )
{
  FAKE_GOP                 Fake;
  UEFI_GUI_DISPLAY_CONFIG  Config;
  UEFI_GUI_DISPLAY_INFO    Info;
  UINT8                    *Memory;
  UINT32                   Source[8 * 4];
  UINT32                   Back[8 * 4];
  UINTN                    Width;
  UINTN                    Height;
  UINTN                    Stride;
  UINTN                    Size;
  UINTN                    Index;
  UINTN                    X;
  UINTN                    Y;
  UINT32                   Native;
  UINT32                   Expected;
  BOOLEAN                  Ok;
  BOOLEAN                  Untouched;

  Print (L"Frame buffer backend, %s:\n", Name);
  Width  = 64;
  Height = 32;
  Stride = 80;                              // pixels per scan line, with padding
  Size   = Stride * Height * BytesPerPixel;
  Memory = UefiGuiAllocate (Size);
  if (Memory == NULL) {
    Check (L"allocate test frame buffer", FALSE);
    return;
  }

  SetMem (Memory, Size, 0xA5);

  ZeroMem (&Config, sizeof (Config));
  Config.Backend = UefiGuiDisplayBackendFrameBuffer;

  //
  // Rejections first.
  //
  FakeGopInit (&Fake, NULL, (UINT32)Width, (UINT32)Height, (UINT32)Stride, Format, Mask, (UINTN)Memory, Size - 1);
  Check (L"frame buffer one byte too small refused", (BOOLEAN)(UefiGuiDisplayInitializeWithGop (&Config, &Fake.Gop) == EFI_UNSUPPORTED));
  FakeGopInit (&Fake, NULL, (UINT32)Width, (UINT32)Height, (UINT32)Width - 1, Format, Mask, (UINTN)Memory, Size);
  Check (L"scan line shorter than width refused", (BOOLEAN)(UefiGuiDisplayInitializeWithGop (&Config, &Fake.Gop) == EFI_UNSUPPORTED));
  FakeGopInit (&Fake, NULL, (UINT32)Width, (UINT32)Height, (UINT32)Stride, Format, Mask, 0, Size);
  Check (L"null frame buffer refused", (BOOLEAN)(UefiGuiDisplayInitializeWithGop (&Config, &Fake.Gop) == EFI_UNSUPPORTED));

  FakeGopInit (&Fake, NULL, (UINT32)Width, (UINT32)Height, (UINT32)Stride, Format, Mask, (UINTN)Memory, Size);
  Check (L"validated and initialized", (BOOLEAN)(UefiGuiDisplayInitializeWithGop (&Config, &Fake.Gop) == EFI_SUCCESS));
  if (EFI_ERROR (UefiGuiDisplayGetInfo (&Info)) || (Info.ActiveBackend != UefiGuiDisplayBackendFrameBuffer)) {
    Check (L"backend is FrameBuffer", FALSE);
    UefiGuiDisplayShutdown ();
    UefiGuiFree (Memory);
    return;
  }

  //
  // Fill the whole visible area, then flush an 8x4 rectangle touching the
  // right edge.
  //
  Check (L"FillRect whole screen", (BOOLEAN)(UefiGuiDisplayFillRect (0x000000, 0, 0, Width, Height) == EFI_SUCCESS));
  for (Index = 0; Index < ARRAY_SIZE (Source); Index++) {
    Source[Index] = (UINT32)((Index * 0x070D13) & 0xFFFFFF);
  }

  Check (
    L"FlushRect at the right edge",
    (BOOLEAN)(UefiGuiDisplayFlushRect (Source, UefiGuiSourceFormatXrgb8888, 8 * sizeof (UINT32), Width - 8, 5, 8, 4) == EFI_SUCCESS)
    );

  Ok = TRUE;
  for (Y = 0; Y < 4; Y++) {
    for (X = 0; X < 8; X++) {
      Native = (BytesPerPixel == 4) ?
               *(UINT32 *)(Memory + ((5 + Y) * Stride + (Width - 8 + X)) * 4) :
               *(UINT16 *)(Memory + ((5 + Y) * Stride + (Width - 8 + X)) * 2);
      Expected = UefiGuiDisplayEncodePixel (Source[Y * 8 + X], Format, Mask);
      if (Native != Expected) {
        Ok = FALSE;
      }
    }
  }

  Check (L"native pixels in memory are correct", Ok);

  Untouched = TRUE;
  for (Y = 0; Y < Height; Y++) {
    for (X = Width; X < Stride; X++) {
      for (Index = 0; Index < BytesPerPixel; Index++) {
        if (Memory[(Y * Stride + X) * BytesPerPixel + Index] != 0xA5) {
          Untouched = FALSE;
        }
      }
    }
  }

  Check (L"scan line padding never written", Untouched);

  Check (L"ReadRect decodes", (BOOLEAN)(UefiGuiDisplayReadRect (Back, Width - 8, 5, 8, 4) == EFI_SUCCESS));
  Ok = TRUE;
  for (Index = 0; Index < ARRAY_SIZE (Source); Index++) {
    if (UefiGuiDisplayEncodePixel (Back[Index], Format, Mask) != UefiGuiDisplayEncodePixel (Source[Index], Format, Mask)) {
      Ok = FALSE;
    }
  }

  Check (L"decoded pixels re-encode identically", Ok);
  UefiGuiDisplayShutdown ();
  UefiGuiFree (Memory);
}

/**
  Entry point.
**/
EFI_STATUS
EFIAPI
GopTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UEFI_GUI_MEMORY_STATS  Before;
  UEFI_GUI_MEMORY_STATS  After;

  Print (L"GopTest - LVGLUefiGuiPkg Phase 1\n");
  UefiGuiGetMemoryStats (&Before);

  TestEncoding ();
  TestRealDisplay ();
  TestModeSwitch ();
  TestBltOnly ();
  TestMemoryFrameBuffer (L"RGB8", PixelRedGreenBlueReserved8BitPerColor, NULL, 4);
  TestMemoryFrameBuffer (L"BGR8", PixelBlueGreenRedReserved8BitPerColor, NULL, 4);
  TestMemoryFrameBuffer (L"BitMask 10:10:10", PixelBitMask, &mMask101010, 4);
  TestMemoryFrameBuffer (L"BitMask 5:6:5 (16 bpp)", PixelBitMask, &mMask565, 2);

  if (UefiGuiDisplayInitialize (NULL) == EFI_SUCCESS) {
    RunTestScreen (HOLD_SECONDS);
    UefiGuiDisplayShutdown ();
  } else {
    Check (L"display for test screen", FALSE);
  }

  UefiGuiGetMemoryStats (&After);
  Print (L"Memory:\n");
  Check (L"no UefiGui memory leaked", (BOOLEAN)(After.CurrentBytes == Before.CurrentBytes));
  Check (L"no invalid frees", (BOOLEAN)(After.InvalidFreeCount == Before.InvalidFreeCount));

  Print (L"%lu checks, %lu failed\n", (UINT64)mChecks, (UINT64)mFailures);
  Print (L"UEFIGUI-GOPTEST: %s\n", (mFailures == 0) ? L"PASS" : L"FAIL");
  return (mFailures == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
