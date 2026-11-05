/** @file
  Visual test screen for the Blt backend, verified by reading it back.

  Layout (all sizes derived from the current resolution):
    - 1-pixel white border on the outermost rows and columns
    - eight color bars (FillRect)
    - XRGB8888 gradient (FlushRect from a padded source with an offset)
    - RGB565 gradient (FlushRect, 16-bit source)
    - sixteen gray steps (FillRect)

  The padding around both gradient sources is magenta; if stride, offset or
  bounds handling were wrong, magenta would show up on screen and the
  read-back comparison would fail.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GopTest.h"

#define PAD_COLUMNS  17
#define PAD_ROWS     5
#define OFFSET_X     3
#define OFFSET_Y     2
#define SENTINEL     0xFF00FF
#define MAX_RESULTS  32

typedef struct {
  CONST CHAR16    *Name;
  BOOLEAN         Passed;
} RESULT;

STATIC RESULT  mResults[MAX_RESULTS];
STATIC UINTN   mResultCount;

STATIC CONST UINT32  mBarColors[8] = {
  0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000
};

/**
  Remember a result; printed after the screen is cleared.
**/
STATIC
VOID
Defer (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed
  )
{
  if (mResultCount < MAX_RESULTS) {
    mResults[mResultCount].Name   = Name;
    mResults[mResultCount].Passed = Passed;
    mResultCount++;
  }
}

/**
  Expected XRGB gradient color at (X, Y) of a Width x Height block.
**/
STATIC
UINT32
XrgbGradient (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  UINT32  Red;
  UINT32  Green;

  Red   = (UINT32)((X * 255) / (Width - 1));
  Green = (UINT32)((Y * 255) / (Height - 1));
  return (Red << 16) | (Green << 8) | 0x80;
}

/**
  RGB565 gradient word at (X, Y) of a Width x Height block.
**/
STATIC
UINT16
Rgb565Gradient (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  UINT32  Red;
  UINT32  Green;
  UINT32  Blue;

  Red   = (UINT32)((X * 31) / (Width - 1));
  Green = (UINT32)((Y * 63) / (Height - 1));
  Blue  = 31 - Red;
  return (UINT16)((Red << 11) | (Green << 5) | Blue);
}

/**
  RGB565 word expanded to 0x00RRGGBB by bit replication.
**/
STATIC
UINT32
Expand565 (
  IN UINT16  Word
  )
{
  UINT32  Red;
  UINT32  Green;
  UINT32  Blue;

  Red   = (Word >> 11) & 0x1F;
  Green = (Word >> 5) & 0x3F;
  Blue  = Word & 0x1F;
  return (((Red << 3) | (Red >> 2)) << 16) | (((Green << 2) | (Green >> 4)) << 8) | ((Blue << 3) | (Blue >> 2));
}

/**
  Read one pixel and compare.
**/
STATIC
BOOLEAN
PixelIs (
  IN UINTN   X,
  IN UINTN   Y,
  IN UINT32  Expected
  )
{
  UINT32  Value;

  if (EFI_ERROR (UefiGuiDisplayReadRect (&Value, X, Y, 1, 1))) {
    return FALSE;
  }

  return (BOOLEAN)(Value == Expected);
}

/**
  Wait for a key press or HoldSeconds, whichever comes first.
**/
STATIC
VOID
Hold (
  IN UINTN  HoldSeconds
  )
{
  EFI_EVENT      Events[2];
  EFI_EVENT      Timer;
  UINTN          Index;
  EFI_INPUT_KEY  Key;

  if (EFI_ERROR (gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Timer))) {
    gBS->Stall (HoldSeconds * 1000000);
    return;
  }

  gBS->SetTimer (Timer, TimerRelative, MultU64x32 (HoldSeconds, 10000000));
  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = Timer;
  if (!EFI_ERROR (gBS->WaitForEvent (2, Events, &Index)) && (Index == 0)) {
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  }

  gBS->CloseEvent (Timer);
}

/**
  Draw the visual test screen with the active backend and verify it by
  reading it back. The screen stays visible for HoldSeconds or until a key
  press; results are printed afterwards.
**/
VOID
RunTestScreen (
  IN UINTN  HoldSeconds
  )
{
  UEFI_GUI_DISPLAY_INFO  Info;
  UINTN                  Width;
  UINTN                  Height;
  UINTN                  Index;
  UINTN                  X;
  UINTN                  Y;
  UINTN                  BarWidth;
  UINTN                  BarTop;
  UINTN                  BarHeight;
  UINTN                  BlockTop;
  UINTN                  BlockWidth;
  UINTN                  BlockHeight;
  UINTN                  Block2Left;
  UINTN                  StepTop;
  UINTN                  StepWidth;
  UINTN                  StepHeight;
  UINTN                  SourceWidth;
  UINT32                 *Xrgb;
  UINT16                 *Rgb565;
  UINT32                 *ReadBack;
  BOOLEAN                Ok;
  BOOLEAN                Drawn;
  UINT32                 Gray;

  mResultCount = 0;
  if (EFI_ERROR (UefiGuiDisplayGetInfo (&Info))) {
    Check (L"test screen: display initialized", FALSE);
    return;
  }

  Width  = Info.HorizontalResolution;
  Height = Info.VerticalResolution;
  if ((Width < 320) || (Height < 240)) {
    Check (L"test screen: resolution at least 320x240", FALSE);
    return;
  }

  BarTop      = 20;
  BarHeight   = Height / 5;
  BarWidth    = (Width - 40) / 8;
  BlockTop    = BarTop + BarHeight + 20;
  BlockWidth  = (Width - 60) / 2;
  BlockHeight = Height / 3;
  Block2Left  = 40 + BlockWidth;
  StepTop     = BlockTop + BlockHeight + 20;
  StepHeight  = Height - StepTop - 20;
  StepWidth   = (Width - 40) / 16;
  SourceWidth = BlockWidth + PAD_COLUMNS;

  Xrgb     = UefiGuiAllocateArray (SourceWidth * (BlockHeight + PAD_ROWS), sizeof (UINT32));
  Rgb565   = UefiGuiAllocateArray (SourceWidth * (BlockHeight + PAD_ROWS), sizeof (UINT16));
  ReadBack = UefiGuiAllocateArray (BlockWidth * BlockHeight, sizeof (UINT32));
  if ((Xrgb == NULL) || (Rgb565 == NULL) || (ReadBack == NULL)) {
    Check (L"test screen: buffers allocated", FALSE);
    UefiGuiFree (Xrgb);
    UefiGuiFree (Rgb565);
    UefiGuiFree (ReadBack);
    return;
  }

  //
  // Sources: magenta everywhere, gradient inside the offset window.
  //
  for (Index = 0; Index < SourceWidth * (BlockHeight + PAD_ROWS); Index++) {
    Xrgb[Index]   = SENTINEL;
    Rgb565[Index] = 0xF81F;
  }

  for (Y = 0; Y < BlockHeight; Y++) {
    for (X = 0; X < BlockWidth; X++) {
      Xrgb[(Y + OFFSET_Y) * SourceWidth + X + OFFSET_X]   = XrgbGradient (X, Y, BlockWidth, BlockHeight);
      Rgb565[(Y + OFFSET_Y) * SourceWidth + X + OFFSET_X] = Rgb565Gradient (X, Y, BlockWidth, BlockHeight);
    }
  }

  //
  // Nothing may be printed from here until the hold ends: console output
  // would draw over (or scroll) the test screen.
  //
  Print (L"GOPTEST: SCREEN READY\n");
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  Drawn = TRUE;
  Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (UefiGuiDisplayFillRect (0x202020, 0, 0, Width, Height)));
  Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (UefiGuiDisplayFillRect (0xFFFFFF, 0, 0, Width, 1)));
  Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (UefiGuiDisplayFillRect (0xFFFFFF, 0, Height - 1, Width, 1)));
  Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (UefiGuiDisplayFillRect (0xFFFFFF, 0, 0, 1, Height)));
  Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (UefiGuiDisplayFillRect (0xFFFFFF, Width - 1, 0, 1, Height)));
  for (Index = 0; Index < 8; Index++) {
    Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (UefiGuiDisplayFillRect (mBarColors[Index], 20 + Index * BarWidth, BarTop, BarWidth, BarHeight)));
  }

  Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (
                               UefiGuiDisplayFlushRect (
                                 &Xrgb[OFFSET_Y * SourceWidth + OFFSET_X],
                                 UefiGuiSourceFormatXrgb8888,
                                 SourceWidth * sizeof (UINT32),
                                 20,
                                 BlockTop,
                                 BlockWidth,
                                 BlockHeight
                                 )
                               ));
  Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (
                               UefiGuiDisplayFlushRect (
                                 &Rgb565[OFFSET_Y * SourceWidth + OFFSET_X],
                                 UefiGuiSourceFormatRgb565,
                                 SourceWidth * sizeof (UINT16),
                                 Block2Left,
                                 BlockTop,
                                 BlockWidth,
                                 BlockHeight
                                 )
                               ));
  for (Index = 0; Index < 16; Index++) {
    Gray  = (UINT32)(Index * 17);
    Drawn = (BOOLEAN)(Drawn && !EFI_ERROR (UefiGuiDisplayFillRect ((Gray << 16) | (Gray << 8) | Gray, 20 + Index * StepWidth, StepTop, StepWidth, StepHeight)));
  }

  Defer (L"test screen: every draw call succeeded", Drawn);

  //
  // Verify by reading the screen back.
  //
  Ok = (BOOLEAN)(PixelIs (0, 0, 0xFFFFFF) && PixelIs (Width - 1, 0, 0xFFFFFF) &&
                 PixelIs (0, Height - 1, 0xFFFFFF) && PixelIs (Width - 1, Height - 1, 0xFFFFFF) &&
                 PixelIs (Width / 2, Height - 1, 0xFFFFFF) && PixelIs (Width - 1, Height / 2, 0xFFFFFF) &&
                 PixelIs (1, 1, 0x202020) && PixelIs (Width - 2, Height - 2, 0x202020));
  Defer (L"read-back: border on the last row and column", Ok);

  Ok = TRUE;
  for (Index = 0; Index < 8; Index++) {
    Ok = (BOOLEAN)(Ok && PixelIs (20 + Index * BarWidth + BarWidth / 2, BarTop + BarHeight / 2, mBarColors[Index]));
    Ok = (BOOLEAN)(Ok && PixelIs (20 + Index * BarWidth, BarTop, mBarColors[Index]));
    Ok = (BOOLEAN)(Ok && PixelIs (20 + (Index + 1) * BarWidth - 1, BarTop + BarHeight - 1, mBarColors[Index]));
  }

  Defer (L"read-back: color bars, centers and corners", Ok);

  Ok = !EFI_ERROR (UefiGuiDisplayReadRect (ReadBack, 20, BlockTop, BlockWidth, BlockHeight));
  for (Y = 0; Ok && (Y < BlockHeight); Y++) {
    for (X = 0; X < BlockWidth; X++) {
      if (ReadBack[Y * BlockWidth + X] != XrgbGradient (X, Y, BlockWidth, BlockHeight)) {
        Ok = FALSE;
        break;
      }
    }
  }

  Defer (L"read-back: XRGB8888 gradient, every pixel (stride + offset)", Ok);
  Defer (L"read-back: no padding drawn left of XRGB block", PixelIs (19, BlockTop + 1, 0x202020));

  Ok = !EFI_ERROR (UefiGuiDisplayReadRect (ReadBack, Block2Left, BlockTop, BlockWidth, BlockHeight));
  for (Y = 0; Ok && (Y < BlockHeight); Y++) {
    for (X = 0; X < BlockWidth; X++) {
      if (ReadBack[Y * BlockWidth + X] != Expand565 (Rgb565Gradient (X, Y, BlockWidth, BlockHeight))) {
        Ok = FALSE;
        break;
      }
    }
  }

  Defer (L"read-back: RGB565 gradient, every pixel", Ok);
  Defer (
    L"read-back: no padding drawn right of RGB565 block",
    PixelIs (Block2Left + BlockWidth, BlockTop + BlockHeight - 1, 0x202020)
    );

  Ok = TRUE;
  for (Index = 0; Index < 16; Index++) {
    Gray = (UINT32)(Index * 17);
    Ok   = (BOOLEAN)(Ok && PixelIs (20 + Index * StepWidth + StepWidth / 2, StepTop + StepHeight / 2, (Gray << 16) | (Gray << 8) | Gray));
  }

  Defer (L"read-back: gray steps", Ok);

  Hold (HoldSeconds);

  UefiGuiFree (Xrgb);
  UefiGuiFree (Rgb565);
  UefiGuiFree (ReadBack);

  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  Print (L"Test screen (%ux%u, Blt backend):\n", (UINT32)Width, (UINT32)Height);
  for (Index = 0; Index < mResultCount; Index++) {
    Check (mResults[Index].Name, mResults[Index].Passed);
  }
}
