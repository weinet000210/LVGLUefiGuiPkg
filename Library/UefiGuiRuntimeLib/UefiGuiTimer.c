/** @file
  Monotonic time base for UefiGui and LVGL.

  The CPU time stamp counter is read with AsmReadTsc() and converted with a
  frequency measured once against gBS->Stall(). On platforms where Stall()
  uses an independent timer (ACPI PM timer, HPET, local APIC) the result is
  accurate to well under one percent; that is enough for LVGL animations and
  for the event loop, which only need relative time.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiGuiRuntimeLib.h>

#define CALIBRATION_US  50000

STATIC UINT64  mTscBase;
STATIC UINT64  mTscPerSecond;

/**
  Calibrate the time base. Idempotent.
**/
EFI_STATUS
EFIAPI
UefiGuiTimeInitialize (
  VOID
  )
{
 #if defined (MDE_CPU_X64) || defined (MDE_CPU_IA32)
  UINT32  Edx;
  UINT64  Start;
  UINT64  End;

  if (mTscPerSecond != 0) {
    return EFI_SUCCESS;
  }

  //
  // CPUID.01h:EDX[4] = TSC present.
  //
  AsmCpuid (1, NULL, NULL, NULL, &Edx);
  if ((Edx & BIT4) == 0) {
    DEBUG ((DEBUG_ERROR, "[UefiGui] time base: CPU has no TSC\n"));
    return EFI_UNSUPPORTED;
  }

  Start = AsmReadTsc ();
  gBS->Stall (CALIBRATION_US);
  End = AsmReadTsc ();
  if (End <= Start) {
    DEBUG ((DEBUG_ERROR, "[UefiGui] time base: TSC did not advance\n"));
    return EFI_UNSUPPORTED;
  }

  mTscPerSecond = DivU64x32 (MultU64x32 (End - Start, 1000), CALIBRATION_US / 1000);
  mTscBase      = End;
  DEBUG ((DEBUG_INFO, "[UefiGui] time base: TSC %lu Hz\n", mTscPerSecond));
  return EFI_SUCCESS;
 #else
  return EFI_UNSUPPORTED;
 #endif
}

/**
  Ticks since calibration, scaled to Units per second.
**/
STATIC
UINT64
UefiGuiTscTo (
  IN UINT32  UnitsPerSecond
  )
{
 #if defined (MDE_CPU_X64) || defined (MDE_CPU_IA32)
  UINT64  Delta;
  UINT64  Seconds;
  UINT64  Remainder;

  if (mTscPerSecond == 0) {
    return 0;
  }

  Delta   = AsmReadTsc () - mTscBase;
  Seconds = DivU64x64Remainder (Delta, mTscPerSecond, &Remainder);
  //
  // Split so that Remainder * UnitsPerSecond cannot overflow for any
  // realistic frequency (Remainder < mTscPerSecond < 2^40).
  //
  return MultU64x32 (Seconds, UnitsPerSecond) +
         DivU64x64Remainder (MultU64x32 (Remainder, UnitsPerSecond), mTscPerSecond, NULL);
 #else
  return 0;
 #endif
}

/**
  Milliseconds since calibration.
**/
UINT64
EFIAPI
UefiGuiGetTimeMs (
  VOID
  )
{
  return UefiGuiTscTo (1000);
}

/**
  Microseconds since calibration.
**/
UINT64
EFIAPI
UefiGuiGetTimeUs (
  VOID
  )
{
  return UefiGuiTscTo (1000000);
}

/**
  Measured TSC frequency.
**/
UINT64
EFIAPI
UefiGuiGetTimeFrequency (
  VOID
  )
{
  return mTscPerSecond;
}
