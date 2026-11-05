/** @file
  UefiGuiHello - Phase 0 smoke test for LVGLUefiGuiPkg.

  1. Prints the package version.
  2. Runs a self-test of the UefiGuiRuntimeLib memory backend.
  3. Counts the GOP and input protocol instances in the handle database
     (detection only; the real backends arrive in later phases).

  The last line is "UEFIGUI-HELLO: PASS" or "UEFIGUI-HELLO: FAIL" so that
  automated QEMU runs can check the result on the serial console.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiGuiRuntimeLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <UefiGui/UefiGuiTypes.h>

STATIC UINTN  mChecks;
STATIC UINTN  mFailures;

/**
  Record and print one check result.
**/
STATIC
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
  TRUE if every byte of Buffer equals Value.
**/
STATIC
BOOLEAN
AllBytesAre (
  IN CONST UINT8  *Buffer,
  IN UINTN        Size,
  IN UINT8        Value
  )
{
  UINTN  Index;

  for (Index = 0; Index < Size; Index++) {
    if (Buffer[Index] != Value) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Exercise every UefiGuiRuntimeLib memory function.
**/
STATIC
VOID
TestMemoryBackend (
  VOID
  )
{
  UEFI_GUI_MEMORY_STATS  Before;
  UEFI_GUI_MEMORY_STATS  After;
  UINT8                  *Buffer;
  UINT8                  *Grown;
  UINT8                  *Shrunk;
  UINT8                  *Small;
  UINT8                  *Big;
  UINTN                  Index;
  BOOLEAN                Pattern;

  Print (L"Memory backend:\n");
  Check (L"GetMemoryStats rejects NULL", (BOOLEAN)(UefiGuiGetMemoryStats (NULL) == EFI_INVALID_PARAMETER));
  Check (L"GetMemoryStats", (BOOLEAN)(UefiGuiGetMemoryStats (&Before) == EFI_SUCCESS));

  Check (L"Allocate(0) returns NULL", (BOOLEAN)(UefiGuiAllocate (0) == NULL));

  Print (L"  (the next two checks print expected allocation errors)\n");
  Check (L"Allocate(MAX_UINTN) is refused", (BOOLEAN)(UefiGuiAllocate (MAX_UINTN) == NULL));
  Check (L"AllocateArray overflow is refused", (BOOLEAN)(UefiGuiAllocateArray ((MAX_UINTN / 2) + 1, 2) == NULL));

  Buffer = UefiGuiAllocateZero (256);
  Check (L"AllocateZero(256)", (BOOLEAN)(Buffer != NULL));
  if (Buffer == NULL) {
    return;
  }

  Check (L"AllocateZero memory is zero", AllBytesAre (Buffer, 256, 0));
  Check (L"AllocationSize reports 256", (BOOLEAN)(UefiGuiAllocationSize (Buffer) == 256));
  Check (L"Payload is 8-byte aligned", (BOOLEAN)(((UINTN)Buffer & 0x7) == 0));

  for (Index = 0; Index < 256; Index++) {
    Buffer[Index] = (UINT8)Index;
  }

  Grown = UefiGuiReallocate (Buffer, 4096);
  Check (L"Reallocate grow to 4096", (BOOLEAN)(Grown != NULL));
  if (Grown == NULL) {
    UefiGuiFree (Buffer);
    return;
  }

  Pattern = TRUE;
  for (Index = 0; Index < 256; Index++) {
    if (Grown[Index] != (UINT8)Index) {
      Pattern = FALSE;
      break;
    }
  }

  Check (L"Reallocate grow keeps contents", Pattern);

  Shrunk = UefiGuiReallocate (Grown, 16);
  Check (L"Reallocate shrink to 16", (BOOLEAN)(Shrunk != NULL));
  if (Shrunk == NULL) {
    UefiGuiFree (Grown);
    return;
  }

  Pattern = TRUE;
  for (Index = 0; Index < 16; Index++) {
    if (Shrunk[Index] != (UINT8)Index) {
      Pattern = FALSE;
      break;
    }
  }

  Check (L"Reallocate shrink keeps contents", Pattern);
  Check (L"Reallocate(p, 0) frees", (BOOLEAN)(UefiGuiReallocate (Shrunk, 0) == NULL));

  //
  // Limit enforcement: allow 1 KiB more than is in use now.
  //
  UefiGuiGetMemoryStats (&After);
  UefiGuiSetMemoryLimit (After.CurrentBytes + 1024);
  Small = UefiGuiAllocate (512);
  Check (L"Allocation under the limit succeeds", (BOOLEAN)(Small != NULL));
  Print (L"  (the next check prints an expected limit error)\n");
  Big = UefiGuiAllocate (2048);
  Check (L"Allocation over the limit is refused", (BOOLEAN)(Big == NULL));
  UefiGuiFree (Big);
  UefiGuiFree (Small);
  UefiGuiSetMemoryLimit (Before.LimitBytes);

  UefiGuiFree (NULL);
  Check (L"Free(NULL) is ignored", TRUE);

  UefiGuiGetMemoryStats (&After);
  Check (L"No bytes leaked", (BOOLEAN)(After.CurrentBytes == Before.CurrentBytes));
  Check (
    L"Allocations and frees balance",
    (BOOLEAN)((After.AllocationCount - Before.AllocationCount) == (After.FreeCount - Before.FreeCount))
    );
  Check (L"Failures were counted", (BOOLEAN)((After.FailureCount - Before.FailureCount) == 3));
  Check (L"No invalid frees", (BOOLEAN)(After.InvalidFreeCount == Before.InvalidFreeCount));

  Print (
    L"  stats: allocs %lu, frees %lu, failures %lu, peak %lu bytes, in use %lu bytes\n",
    After.AllocationCount,
    After.FreeCount,
    After.FailureCount,
    After.PeakBytes,
    After.CurrentBytes
    );
}

/**
  Print the number of handles that carry Protocol.

  @return  Handle count.
**/
STATIC
UINTN
ReportProtocol (
  IN CONST CHAR16  *Name,
  IN EFI_GUID      *Protocol
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       Count;

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Count = 0;
  }

  if (Handles != NULL) {
    gBS->FreePool (Handles);
  }

  Print (L"  %-24s %lu instance(s)\n", Name, (UINT64)Count);
  return Count;
}

/**
  Describe the current GOP mode without changing it.
**/
STATIC
VOID
ReportGop (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  STATIC CONST CHAR16           *FormatNames[] = {
    L"PixelRedGreenBlueReserved8BitPerColor",
    L"PixelBlueGreenRedReserved8BitPerColor",
    L"PixelBitMask",
    L"PixelBltOnly"
  };

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) {
    Print (L"  GOP: not available (%r)\n", Status);
    return;
  }

  Print (
    L"  GOP mode %u of %u: %ux%u, stride %u, %s\n",
    Gop->Mode->Mode,
    Gop->Mode->MaxMode,
    Gop->Mode->Info->HorizontalResolution,
    Gop->Mode->Info->VerticalResolution,
    Gop->Mode->Info->PixelsPerScanLine,
    ((UINTN)Gop->Mode->Info->PixelFormat < ARRAY_SIZE (FormatNames)) ?
    FormatNames[Gop->Mode->Info->PixelFormat] : L"unknown"
    );
}

/**
  Entry point.

  @param[in] ImageHandle  Image handle.
  @param[in] SystemTable  System table.

  @retval EFI_SUCCESS   All checks passed.
  @retval EFI_ABORTED   At least one check failed.
**/
EFI_STATUS
EFIAPI
UefiGuiHelloMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  Print (
    L"UefiGuiHello - LVGLUefiGuiPkg v%u.%u (Phase 0)\n",
    (UINT32)UEFI_GUI_VERSION_MAJOR,
    (UINT32)UEFI_GUI_VERSION_MINOR
    );

  TestMemoryBackend ();

  Print (L"Firmware protocols:\n");
  ReportProtocol (L"GraphicsOutput", &gEfiGraphicsOutputProtocolGuid);
  ReportProtocol (L"SimpleTextInputEx", &gEfiSimpleTextInputExProtocolGuid);
  ReportProtocol (L"SimpleTextInput", &gEfiSimpleTextInProtocolGuid);
  ReportProtocol (L"SimplePointer", &gEfiSimplePointerProtocolGuid);
  ReportProtocol (L"AbsolutePointer", &gEfiAbsolutePointerProtocolGuid);
  ReportGop ();

  Print (L"%lu checks, %lu failed\n", (UINT64)mChecks, (UINT64)mFailures);
  Print (L"UEFIGUI-HELLO: %s\n", (mFailures == 0) ? L"PASS" : L"FAIL");

  return (mFailures == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
