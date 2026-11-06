## @file
#  Build description for LVGLUefiGuiPkg libraries, tests and sample apps.
#
#  Linux:   build -p LVGLUefiGuiPkg/LVGLUefiGuiPkg.dsc -a X64 -t GCC
#  Windows: build -p LVGLUefiGuiPkg\LVGLUefiGuiPkg.dsc -a X64 -t VS2022
#
#  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[Defines]
  PLATFORM_NAME                  = LVGLUefiGuiPkg
  PLATFORM_GUID                  = 1758140F-82E7-47D7-B059-4BD70059328F
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010006
  OUTPUT_DIRECTORY               = Build/LVGLUefiGuiPkg
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses.common]
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
!if $(TARGET) == RELEASE
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!else
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
!endif

  #
  # LVGLUefiGuiPkg library instances
  #
  UefiGuiRuntimeLib|LVGLUefiGuiPkg/Library/UefiGuiRuntimeLib/UefiGuiRuntimeLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042

[Components]
  LVGLUefiGuiPkg/Library/UefiGuiRuntimeLib/UefiGuiRuntimeLib.inf
  LVGLUefiGuiPkg/Application/UefiGuiHello/UefiGuiHello.inf
