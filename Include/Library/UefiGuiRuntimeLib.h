/** @file
  UefiGuiRuntimeLib - memory allocation and the time base for UefiGui and
  LVGL.

  All memory used by UefiGui and by LVGL (through the LV_STDLIB_CUSTOM hooks
  added in Phase 2) comes from these functions. They wrap the UEFI pool
  allocator (AllocatePool / AllocateZeroPool / FreePool) and add:
    - integer overflow checks on every size computation,
    - an optional upper limit on outstanding memory,
    - usage statistics for diagnostics,
    - detection of frees of pointers that were not allocated here.

  Memory returned by UefiGuiAllocate() must only be released with
  UefiGuiFree() or resized with UefiGuiReallocate(). Never pass it to
  FreePool(), and never pass AllocatePool() memory to UefiGuiFree().

  Must be called at TPL_APPLICATION or TPL_CALLBACK (pool services allow up
  to TPL_NOTIFY, but UefiGui keeps all GUI work at TPL_APPLICATION).

  Time base (Phase 2): a monotonic millisecond / microsecond clock built on
  the CPU time stamp counter, calibrated once against gBS->Stall(). It feeds
  lv_tick and the event loop. UEFI has no wall-clock-free monotonic timer
  service, and timer events only fire at the platform tick (often 10 ms),
  so they are not used as a clock.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_RUNTIME_LIB_H_
#define UEFI_GUI_RUNTIME_LIB_H_

#include <UefiGui/UefiGuiTypes.h>

/**
  Allocate a buffer from the UEFI pool.

  @param[in]  Size   Number of bytes. 0 returns NULL.

  @return  Pointer to the buffer, or NULL if Size is 0, the size overflows,
           the configured limit would be exceeded or the pool is exhausted.
**/
VOID *
EFIAPI
UefiGuiAllocate (
  IN UINTN  Size
  );

/**
  Allocate a zero-filled buffer from the UEFI pool.

  @param[in]  Size   Number of bytes. 0 returns NULL.

  @return  Pointer to the zeroed buffer, or NULL on failure.
**/
VOID *
EFIAPI
UefiGuiAllocateZero (
  IN UINTN  Size
  );

/**
  Allocate a zero-filled array, checking Count * ElementSize for overflow.

  @return  Pointer to the zeroed array, or NULL on failure or overflow.
**/
VOID *
EFIAPI
UefiGuiAllocateArray (
  IN UINTN  Count,
  IN UINTN  ElementSize
  );

/**
  Resize a buffer allocated by UefiGuiAllocate().

  Behaves like C realloc(): Buffer == NULL allocates, NewSize == 0 frees and
  returns NULL. On failure NULL is returned and Buffer is left untouched.
  The contents up to the smaller of the old and new size are preserved.

  @return  Pointer to the resized buffer, or NULL.
**/
VOID *
EFIAPI
UefiGuiReallocate (
  IN VOID   *Buffer  OPTIONAL,
  IN UINTN  NewSize
  );

/**
  Free a buffer allocated by UefiGuiAllocate(), UefiGuiAllocateZero(),
  UefiGuiAllocateArray() or UefiGuiReallocate(). NULL is ignored.

  A pointer that was not allocated by UefiGui is not freed; it is counted in
  InvalidFreeCount and reported with DEBUG_ERROR (and ASSERT in DEBUG builds).
**/
VOID
EFIAPI
UefiGuiFree (
  IN VOID  *Buffer  OPTIONAL
  );

/**
  Return the size requested when Buffer was allocated.

  @return  Payload size in bytes, or 0 for NULL or a foreign pointer.
**/
UINTN
EFIAPI
UefiGuiAllocationSize (
  IN CONST VOID  *Buffer  OPTIONAL
  );

/**
  Set the limit on outstanding UefiGui memory. 0 removes the limit.
  Allocations already made are not affected.
**/
VOID
EFIAPI
UefiGuiSetMemoryLimit (
  IN UINT64  LimitBytes
  );

/**
  Read the memory statistics.

  @retval EFI_SUCCESS            Stats filled.
  @retval EFI_INVALID_PARAMETER  Stats is NULL.
**/
EFI_STATUS
EFIAPI
UefiGuiGetMemoryStats (
  OUT UEFI_GUI_MEMORY_STATS  *Stats
  );

/**
  Calibrate the time base. Idempotent; takes about 50 ms the first time.

  @retval EFI_SUCCESS      Time base ready.
  @retval EFI_UNSUPPORTED  The CPU has no time stamp counter, or calibration
                           measured no progress.
**/
EFI_STATUS
EFIAPI
UefiGuiTimeInitialize (
  VOID
  );

/**
  Milliseconds since UefiGuiTimeInitialize() first succeeded, 0 before.
  Monotonic.
**/
UINT64
EFIAPI
UefiGuiGetTimeMs (
  VOID
  );

/**
  Microseconds since UefiGuiTimeInitialize() first succeeded, 0 before.
  Monotonic.
**/
UINT64
EFIAPI
UefiGuiGetTimeUs (
  VOID
  );

/**
  Time stamp counter ticks per second measured by the calibration, 0 before.
**/
UINT64
EFIAPI
UefiGuiGetTimeFrequency (
  VOID
  );

#endif // UEFI_GUI_RUNTIME_LIB_H_
