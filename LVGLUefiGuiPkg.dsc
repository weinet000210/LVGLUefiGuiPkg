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
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf
  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
!if $(TARGET) == RELEASE
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!else
  #
  # DEBUG() goes straight to the COM1 UART, never to ConOut: a GUI owns the
  # screen, and under the UEFI Shell a ConOut write blocks while the Shell's
  # output is paused (Ctrl+S).
  #
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  SerialPortLib|MdeModulePkg/Library/BaseSerialPortLib16550/BaseSerialPortLib16550.inf
  PlatformHookLib|MdeModulePkg/Library/BasePlatformHookLibNull/BasePlatformHookLibNull.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  PciLib|MdePkg/Library/BasePciLibCf8/BasePciLibCf8.inf
  PciCf8Lib|MdePkg/Library/BasePciCf8Lib/BasePciCf8Lib.inf
!endif

  #
  # LVGLUefiGuiPkg library instances
  #
  UefiGuiRuntimeLib|LVGLUefiGuiPkg/Library/UefiGuiRuntimeLib/UefiGuiRuntimeLib.inf
  UefiGuiDisplayLib|LVGLUefiGuiPkg/Library/UefiGuiDisplayLib/UefiGuiDisplayLib.inf
  UefiGuiInputLib|LVGLUefiGuiPkg/Library/UefiGuiInputLib/UefiGuiInputLib.inf
  UefiGuiLib|LVGLUefiGuiPkg/Library/UefiGuiLib/UefiGuiLib.inf
  LvglLib|LVGLUefiGuiPkg/Library/LvglLib/LvglLib.inf
  CompilerIntrinsicsLib|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042

[Components]
  LVGLUefiGuiPkg/Library/UefiGuiRuntimeLib/UefiGuiRuntimeLib.inf
  LVGLUefiGuiPkg/Library/UefiGuiDisplayLib/UefiGuiDisplayLib.inf
  LVGLUefiGuiPkg/Library/LvglLib/LvglLib.inf
  LVGLUefiGuiPkg/Library/UefiGuiInputLib/UefiGuiInputLib.inf
  LVGLUefiGuiPkg/Library/UefiGuiLib/UefiGuiLib.inf
  LVGLUefiGuiPkg/Application/UefiGuiHello/UefiGuiHello.inf
  LVGLUefiGuiPkg/Test/GopTest/GopTest.inf
  LVGLUefiGuiPkg/Test/LvglPortTest/LvglPortTest.inf
  LVGLUefiGuiPkg/Test/InputTest/InputTest.inf
  LVGLUefiGuiPkg/Test/RuntimeTest/RuntimeTest.inf
  LVGLUefiGuiPkg/Test/PerfTest/PerfTest.inf

  #
  # USB mouse drivers that OVMF does not include. Built only so the QEMU
  # tests can load them from the UEFI Shell (scripts/run-qemu.sh --load) to
  # exercise the Simple Pointer and Absolute Pointer paths. Not part of the
  # framework.
  #
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseAbsolutePointerDxe/UsbMouseAbsolutePointerDxe.inf
