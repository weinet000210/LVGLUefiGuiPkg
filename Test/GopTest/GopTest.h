/** @file
  Shared declarations of the GopTest application.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GOP_TEST_H_
#define GOP_TEST_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiGuiDisplayLib.h>
#include <Library/UefiGuiRuntimeLib.h>
#include <Protocol/GraphicsOutput.h>

///
/// A GOP that reports a chosen mode. Blt() is forwarded to a real GOP (or
/// fails when Real is NULL); the frame buffer may point to plain memory.
///
typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL            Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE       Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    Info;
  EFI_GRAPHICS_OUTPUT_PROTOCOL            *Real;
} FAKE_GOP;

/**
  Record and print one check result (only printed when it fails, or when
  Verbose is TRUE).
**/
VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Passed
  );

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
  );

/**
  Draw the visual test screen with the Blt backend and verify it by reading
  it back. The screen stays visible for HoldSeconds or until a key press.
**/
VOID
RunTestScreen (
  IN UINTN  HoldSeconds
  );

#endif // GOP_TEST_H_
