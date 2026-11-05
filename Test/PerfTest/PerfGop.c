/** @file
  PerfTest section 1: GOP Blt latency and the UefiGuiDisplayLib backends.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "PerfTest.h"

#define MIN_REPS     10
#define MAX_REPS     400
#define MIN_TIME_US  150000

typedef struct {
  CONST CHAR8    *Name;
  UINT32         Width;    ///< 0 = display width
  UINT32         Height;   ///< 0 = display height
} RECT_SIZE;

STATIC CONST RECT_SIZE  mSizes[] = {
  { "16x16",   16,  16  },
  { "64x64",   64,  64  },
  { "256x256", 256, 256 },
  { "band64",  0,   64  },
  { "full",    0,   0   },
};

STATIC PERF_SAMPLES  mSamples;

typedef enum {
  OpBufferToVideo,
  OpVideoFill,
  OpVideoToBuffer,
  OpFlushBlt,
  OpFlushRgb565,
} PERF_OP;

/**
  Time one operation repeatedly on a WxH rectangle at varying positions.

  @return Number of calls that failed.
**/
STATIC
UINTN
TimeOp (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN PERF_OP                        Op,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer,
  IN VOID                           *Source,
  IN UINTN                          ScreenWidth,
  IN UINTN                          ScreenHeight,
  IN UINTN                          Width,
  IN UINTN                          Height
  )
{
  EFI_STATUS  Status;
  UINT64      Begin;
  UINT64      Start;
  UINTN       Failures;
  UINTN       X;
  UINTN       Y;
  UINTN       Stride;

  mSamples.Count = 0;
  Failures       = 0;
  Begin          = UefiGuiGetTimeUs ();
  while ((mSamples.Count < MAX_REPS) &&
         ((mSamples.Count < MIN_REPS) || (UefiGuiGetTimeUs () - Begin < MIN_TIME_US)))
  {
    X = (Width < ScreenWidth) ? (Random32 () % (ScreenWidth - Width + 1)) : 0;
    Y = (Height < ScreenHeight) ? (Random32 () % (ScreenHeight - Height + 1)) : 0;
    Start = UefiGuiGetTimeUs ();
    switch (Op) {
      case OpBufferToVideo:
        Status = Gop->Blt (Gop, BltBuffer, EfiBltBufferToVideo, 0, 0, X, Y, Width, Height, ScreenWidth * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
        break;
      case OpVideoFill:
        Status = Gop->Blt (Gop, BltBuffer, EfiBltVideoFill, 0, 0, X, Y, Width, Height, 0);
        break;
      case OpVideoToBuffer:
        Status = Gop->Blt (Gop, BltBuffer, EfiBltVideoToBltBuffer, X, Y, 0, 0, Width, Height, ScreenWidth * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
        break;
      case OpFlushBlt:
        Status = UefiGuiDisplayFlushRect (Source, UefiGuiSourceFormatXrgb8888, ScreenWidth * 4, X, Y, Width, Height);
        break;
      default:
        Stride = ScreenWidth * 2;
        Status = UefiGuiDisplayFlushRect (Source, UefiGuiSourceFormatRgb565, Stride, X, Y, Width, Height);
        break;
    }

    SampleAdd (&mSamples, UefiGuiGetTimeUs () - Start);
    if (EFI_ERROR (Status)) {
      Failures++;
    }
  }

  return Failures;
}

/**
  Fill an XRGB8888 buffer with a gradient whose value is a function of the
  position, so read-back can verify any pixel.
**/
STATIC
UINT32
PatternAt (
  IN UINTN  X,
  IN UINTN  Y
  )
{
  return (UINT32)((((X * 255) / 1024) & 0xFF) << 16) | (UINT32)((((Y * 255) / 768) & 0xFF) << 8) | (UINT32)((X ^ Y) & 0xFF);
}

/**
  Measure one display backend through UefiGuiDisplayFlushRect().
**/
STATIC
VOID
MeasureBackend (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UEFI_GUI_DISPLAY_BACKEND      Backend,
  IN CONST CHAR8                   *Tag,
  IN UINT32                        *Xrgb,
  IN UINT16                        *Rgb565
  )
{
  UEFI_GUI_DISPLAY_CONFIG  Config;
  UEFI_GUI_DISPLAY_INFO    Info;
  EFI_STATUS               Status;
  UINTN                    Index;
  UINTN                    Failures;
  UINTN                    W;
  UINTN                    H;
  UINT32                   Read[16];
  UINTN                    Point;
  UINTN                    Mismatch;
  UINTN                    Px;
  UINTN                    Py;
  CHAR8                    Prefix[64];
  PERF_SUMMARY             Summary;

  ZeroMem (&Config, sizeof (Config));
  Config.Backend         = Backend;
  Config.DrawBufferLines = 64;
  Status                 = UefiGuiDisplayInitializeWithGop (&Config, Gop);
  if (Backend == UefiGuiDisplayBackendFrameBuffer) {
    MetricF (EFI_ERROR (Status) ? 0 : 1, "bool", "display.%a.supported", Tag);
    Check (L"1 FrameBuffer backend: initialized, or refused with EFI_UNSUPPORTED", (BOOLEAN)(!EFI_ERROR (Status) || (Status == EFI_UNSUPPORTED)));
    if (EFI_ERROR (Status)) {
      return;
    }
  } else {
    Check (L"1 Blt backend initialized", (BOOLEAN)!EFI_ERROR (Status));
    if (EFI_ERROR (Status)) {
      return;
    }
  }

  UefiGuiDisplayGetInfo (&Info);
  Failures = 0;
  for (Index = 0; Index < ARRAY_SIZE (mSizes); Index++) {
    W         = (mSizes[Index].Width == 0) ? Info.HorizontalResolution : mSizes[Index].Width;
    H         = (mSizes[Index].Height == 0) ? Info.VerticalResolution : mSizes[Index].Height;
    Failures += TimeOp (Gop, OpFlushBlt, NULL, Xrgb, Info.HorizontalResolution, Info.VerticalResolution, W, H);
    AsciiSPrint (Prefix, sizeof (Prefix), "display.%a.xrgb8888.%a", Tag, mSizes[Index].Name);
    MetricSummary (Prefix, &mSamples, &Summary);
    if (Summary.Median != 0) {
      MetricF (DivU64x64Remainder ((UINT64)W * H, Summary.Median, NULL), "Mpx/s", "%a.throughput", Prefix);
    }
  }

  W         = Info.HorizontalResolution;
  H         = Info.VerticalResolution;
  Failures += TimeOp (Gop, OpFlushRgb565, NULL, Rgb565, W, H, W, H);
  AsciiSPrint (Prefix, sizeof (Prefix), "display.%a.rgb565.full", Tag);
  MetricSummary (Prefix, &mSamples, NULL);
  CheckValue (
    (Backend == UefiGuiDisplayBackendBlt) ? L"1 Blt backend: every flush succeeded" : L"1 FrameBuffer backend: every flush succeeded",
    (BOOLEAN)(Failures == 0),
    (INT64)Failures
    );

  //
  // Draw the whole pattern once more and read it back through GOP Blt.
  //
  Status   = UefiGuiDisplayFlushRect (Xrgb, UefiGuiSourceFormatXrgb8888, W * 4, 0, 0, W, H);
  Mismatch = 0;
  for (Point = 0; Point < 8; Point++) {
    Px = (Random32 () % (W - 4));
    Py = (Random32 () % (H - 4));
    if (EFI_ERROR (UefiGuiDisplayReadRect (Read, Px, Py, 4, 4))) {
      Mismatch++;
      continue;
    }

    for (Index = 0; Index < 16; Index++) {
      if ((Read[Index] & 0xFFFFFF) != (Xrgb[(Py + Index / 4) * W + Px + Index % 4] & 0xFFFFFF)) {
        Mismatch++;
      }
    }
  }

  CheckValue (
    (Backend == UefiGuiDisplayBackendBlt) ? L"1 Blt backend: read-back matches the source" : L"1 FrameBuffer backend: read-back matches the source",
    (BOOLEAN)(!EFI_ERROR (Status) && (Mismatch == 0)),
    (INT64)Mismatch
    );
  UefiGuiDisplayShutdown ();
}

VOID
SectionGop (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Blt;
  UINT32                         *Xrgb;
  UINT16                         *Rgb565;
  UINTN                          W;
  UINTN                          H;
  UINTN                          X;
  UINTN                          Y;
  UINTN                          Index;
  UINTN                          Failures;
  UINT32                         Rgb;
  CHAR8                          Prefix[64];
  PERF_SUMMARY                   Summary;
  UINTN                          Rw;
  UINTN                          Rh;

  Gop = FindGop ();
  W   = Gop->Mode->Info->HorizontalResolution;
  H   = Gop->Mode->Info->VerticalResolution;
  Blt    = AllocatePool (W * H * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  Xrgb   = AllocatePool (W * H * sizeof (UINT32));
  Rgb565 = AllocatePool (W * H * sizeof (UINT16));
  Check (L"1 GOP buffers allocated", (BOOLEAN)((Blt != NULL) && (Xrgb != NULL) && (Rgb565 != NULL)));
  if ((Blt == NULL) || (Xrgb == NULL) || (Rgb565 == NULL)) {
    goto Done;
  }

  for (Y = 0; Y < H; Y++) {
    for (X = 0; X < W; X++) {
      Rgb                       = PatternAt (X, Y);
      Xrgb[Y * W + X]           = Rgb;
      Blt[Y * W + X].Red        = (UINT8)(Rgb >> 16);
      Blt[Y * W + X].Green      = (UINT8)(Rgb >> 8);
      Blt[Y * W + X].Blue       = (UINT8)Rgb;
      Blt[Y * W + X].Reserved   = 0;
      Rgb565[Y * W + X]         = (UINT16)(((Rgb >> 8) & 0xF800) | ((Rgb >> 5) & 0x07E0) | ((Rgb >> 3) & 0x001F));
    }
  }

  //
  // Raw GOP.
  //
  Failures = 0;
  for (Index = 0; Index < ARRAY_SIZE (mSizes); Index++) {
    Rw        = (mSizes[Index].Width == 0) ? W : mSizes[Index].Width;
    Rh        = (mSizes[Index].Height == 0) ? H : mSizes[Index].Height;
    Failures += TimeOp (Gop, OpBufferToVideo, Blt, NULL, W, H, Rw, Rh);
    AsciiSPrint (Prefix, sizeof (Prefix), "gop.buffer_to_video.%a", mSizes[Index].Name);
    MetricSummary (Prefix, &mSamples, &Summary);
    if (Summary.Median != 0) {
      MetricF (DivU64x64Remainder ((UINT64)Rw * Rh, Summary.Median, NULL), "Mpx/s", "%a.throughput", Prefix);
    }
  }

  Failures += TimeOp (Gop, OpVideoFill, Blt, NULL, W, H, W, H);
  MetricSummary ("gop.video_fill.full", &mSamples, NULL);
  Failures += TimeOp (Gop, OpVideoToBuffer, Blt, NULL, W, H, 64, 64);
  MetricSummary ("gop.video_to_buffer.64x64", &mSamples, NULL);
  Failures += TimeOp (Gop, OpVideoToBuffer, Blt, NULL, W, H, W, H);
  MetricSummary ("gop.video_to_buffer.full", &mSamples, NULL);
  CheckValue (L"1 raw GOP Blt: every call returned EFI_SUCCESS", (BOOLEAN)(Failures == 0), (INT64)Failures);

  //
  // UefiGuiDisplayLib backends on the same rectangles.
  //
  MeasureBackend (Gop, UefiGuiDisplayBackendBlt, "blt", Xrgb, Rgb565);
  MeasureBackend (Gop, UefiGuiDisplayBackendFrameBuffer, "framebuffer", Xrgb, Rgb565);

Done:
  if (Blt != NULL) {
    FreePool (Blt);
  }

  if (Xrgb != NULL) {
    FreePool (Xrgb);
  }

  if (Rgb565 != NULL) {
    FreePool (Rgb565);
  }
}
