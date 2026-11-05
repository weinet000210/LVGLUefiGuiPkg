/** @file
  Internal definitions of UefiGuiRuntimeLib.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_RUNTIME_INTERNAL_H_
#define UEFI_GUI_RUNTIME_INTERNAL_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiGuiRuntimeLib.h>
#include <UefiGui/UefiGuiStatus.h>

#define UEFI_GUI_MEM_SIGNATURE        SIGNATURE_32 ('U', 'G', 'M', 'A')
#define UEFI_GUI_MEM_FREED_SIGNATURE  SIGNATURE_32 ('U', 'G', 'M', 'F')

///
/// Header placed in front of every UefiGui allocation. 16 bytes, so the
/// payload keeps the alignment the UEFI pool gives the block (8 bytes or
/// more).
///
typedef struct {
  UINT32    Signature;
  UINT32    Reserved;
  UINT64    Size;
} UEFI_GUI_MEM_HEADER;

#endif // UEFI_GUI_RUNTIME_INTERNAL_H_
