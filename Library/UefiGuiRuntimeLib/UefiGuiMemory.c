/** @file
  UefiGui memory backend on top of the UEFI pool allocator.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiRuntimeInternal.h"

STATIC UEFI_GUI_MEMORY_STATS  mUefiGuiMemStats = {
  0, 0, FixedPcdGet64 (PcdUefiGuiMemoryLimit), 0, 0, 0, 0
};

/**
  Return the header of a UefiGui allocation, or NULL if Buffer was not
  allocated here (or was already freed).
**/
STATIC
UEFI_GUI_MEM_HEADER *
UefiGuiMemHeader (
  IN CONST VOID  *Buffer
  )
{
  UEFI_GUI_MEM_HEADER  *Header;

  if (Buffer == NULL) {
    return NULL;
  }

  //
  // A valid pool pointer is at least 8-byte aligned and lies above the
  // header. Anything else cannot be ours; do not dereference it.
  //
  if ((((UINTN)Buffer & 0x7) != 0) || ((UINTN)Buffer < sizeof (UEFI_GUI_MEM_HEADER))) {
    return NULL;
  }

  Header = (UEFI_GUI_MEM_HEADER *)((UINT8 *)Buffer - sizeof (UEFI_GUI_MEM_HEADER));
  if (Header->Signature != UEFI_GUI_MEM_SIGNATURE) {
    return NULL;
  }

  return Header;
}

/**
  Common allocation path.
**/
STATIC
VOID *
UefiGuiAllocateInternal (
  IN UINTN    Size,
  IN BOOLEAN  Zero
  )
{
  UEFI_GUI_MEM_HEADER  *Header;
  UINTN                Total;

  if (Size == 0) {
    return NULL;
  }

  if (Size > MAX_UINTN - sizeof (UEFI_GUI_MEM_HEADER)) {
    mUefiGuiMemStats.FailureCount++;
    DEBUG ((DEBUG_ERROR, "[UefiGui] %a: size 0x%lx overflows\n", __func__, (UINT64)Size));
    return NULL;
  }

  if ((mUefiGuiMemStats.LimitBytes != 0) &&
      ((UINT64)Size > mUefiGuiMemStats.LimitBytes - MIN (mUefiGuiMemStats.LimitBytes, mUefiGuiMemStats.CurrentBytes)))
  {
    mUefiGuiMemStats.FailureCount++;
    DEBUG ((
      DEBUG_ERROR,
      "[UefiGui] %a: 0x%lx bytes would exceed limit 0x%lx (in use 0x%lx)\n",
      __func__,
      (UINT64)Size,
      mUefiGuiMemStats.LimitBytes,
      mUefiGuiMemStats.CurrentBytes
      ));
    return NULL;
  }

  Total = Size + sizeof (UEFI_GUI_MEM_HEADER);
  if (Zero) {
    Header = AllocateZeroPool (Total);
  } else {
    Header = AllocatePool (Total);
  }

  if (Header == NULL) {
    mUefiGuiMemStats.FailureCount++;
    DEBUG ((DEBUG_ERROR, "[UefiGui] %a: pool exhausted for 0x%lx bytes\n", __func__, (UINT64)Size));
    return NULL;
  }

  Header->Signature = UEFI_GUI_MEM_SIGNATURE;
  Header->Reserved  = 0;
  Header->Size      = (UINT64)Size;

  mUefiGuiMemStats.CurrentBytes += (UINT64)Size;
  mUefiGuiMemStats.AllocationCount++;
  if (mUefiGuiMemStats.CurrentBytes > mUefiGuiMemStats.PeakBytes) {
    mUefiGuiMemStats.PeakBytes = mUefiGuiMemStats.CurrentBytes;
  }

  return (VOID *)(Header + 1);
}

/**
  Allocate a buffer from the UEFI pool.

  @param[in]  Size   Number of bytes. 0 returns NULL.

  @return  Pointer to the buffer, or NULL on failure.
**/
VOID *
EFIAPI
UefiGuiAllocate (
  IN UINTN  Size
  )
{
  return UefiGuiAllocateInternal (Size, FALSE);
}

/**
  Allocate a zero-filled buffer from the UEFI pool.

  @param[in]  Size   Number of bytes. 0 returns NULL.

  @return  Pointer to the zeroed buffer, or NULL on failure.
**/
VOID *
EFIAPI
UefiGuiAllocateZero (
  IN UINTN  Size
  )
{
  return UefiGuiAllocateInternal (Size, TRUE);
}

/**
  Allocate a zero-filled array, checking Count * ElementSize for overflow.

  @return  Pointer to the zeroed array, or NULL on failure or overflow.
**/
VOID *
EFIAPI
UefiGuiAllocateArray (
  IN UINTN  Count,
  IN UINTN  ElementSize
  )
{
  if ((Count == 0) || (ElementSize == 0)) {
    return NULL;
  }

  if (Count > MAX_UINTN / ElementSize) {
    mUefiGuiMemStats.FailureCount++;
    DEBUG ((
      DEBUG_ERROR,
      "[UefiGui] %a: 0x%lx x 0x%lx overflows\n",
      __func__,
      (UINT64)Count,
      (UINT64)ElementSize
      ));
    return NULL;
  }

  return UefiGuiAllocateInternal (Count * ElementSize, TRUE);
}

/**
  Free a buffer allocated by UefiGui. NULL is ignored. Foreign or already
  freed pointers are reported and not released.
**/
VOID
EFIAPI
UefiGuiFree (
  IN VOID  *Buffer  OPTIONAL
  )
{
  UEFI_GUI_MEM_HEADER  *Header;

  if (Buffer == NULL) {
    return;
  }

  Header = UefiGuiMemHeader (Buffer);
  if (Header == NULL) {
    mUefiGuiMemStats.InvalidFreeCount++;
    DEBUG ((DEBUG_ERROR, "[UefiGui] %a: %p is not a UefiGui allocation (or freed twice)\n", __func__, Buffer));
    ASSERT (FALSE);
    return;
  }

  ASSERT (mUefiGuiMemStats.CurrentBytes >= Header->Size);
  mUefiGuiMemStats.CurrentBytes -= MIN (mUefiGuiMemStats.CurrentBytes, Header->Size);
  mUefiGuiMemStats.FreeCount++;

  //
  // Poison the signature so a second free of the same pointer is caught
  // while the pool block has not been reused yet.
  //
  Header->Signature = UEFI_GUI_MEM_FREED_SIGNATURE;
  Header->Size      = 0;
  FreePool (Header);
}

/**
  Resize a buffer allocated by UefiGui, with C realloc() semantics.

  @return  Pointer to the resized buffer, or NULL.
**/
VOID *
EFIAPI
UefiGuiReallocate (
  IN VOID   *Buffer  OPTIONAL,
  IN UINTN  NewSize
  )
{
  UEFI_GUI_MEM_HEADER  *Header;
  VOID                 *NewBuffer;
  UINTN                OldSize;

  if (Buffer == NULL) {
    return UefiGuiAllocate (NewSize);
  }

  if (NewSize == 0) {
    UefiGuiFree (Buffer);
    return NULL;
  }

  Header = UefiGuiMemHeader (Buffer);
  if (Header == NULL) {
    mUefiGuiMemStats.InvalidFreeCount++;
    DEBUG ((DEBUG_ERROR, "[UefiGui] %a: %p is not a UefiGui allocation\n", __func__, Buffer));
    ASSERT (FALSE);
    return NULL;
  }

  OldSize = (UINTN)Header->Size;
  if (NewSize == OldSize) {
    return Buffer;
  }

  NewBuffer = UefiGuiAllocate (NewSize);
  if (NewBuffer == NULL) {
    return NULL;
  }

  CopyMem (NewBuffer, Buffer, MIN (OldSize, NewSize));
  UefiGuiFree (Buffer);
  return NewBuffer;
}

/**
  Return the size requested when Buffer was allocated, or 0.
**/
UINTN
EFIAPI
UefiGuiAllocationSize (
  IN CONST VOID  *Buffer  OPTIONAL
  )
{
  UEFI_GUI_MEM_HEADER  *Header;

  Header = UefiGuiMemHeader (Buffer);
  if (Header == NULL) {
    return 0;
  }

  return (UINTN)Header->Size;
}

/**
  Set the limit on outstanding UefiGui memory. 0 removes the limit.
**/
VOID
EFIAPI
UefiGuiSetMemoryLimit (
  IN UINT64  LimitBytes
  )
{
  mUefiGuiMemStats.LimitBytes = LimitBytes;
}

/**
  Read the memory statistics.

  @retval EFI_SUCCESS            Stats filled.
  @retval EFI_INVALID_PARAMETER  Stats is NULL.
**/
EFI_STATUS
EFIAPI
UefiGuiGetMemoryStats (
  OUT UEFI_GUI_MEMORY_STATS  *Stats
  )
{
  if (Stats == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Stats, &mUefiGuiMemStats, sizeof (*Stats));
  return EFI_SUCCESS;
}
