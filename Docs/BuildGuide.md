# Build Guide

LVGLUefiGuiPkg builds as an ordinary EDK2 package. No file inside the edk2
tree is modified; the package is found through `PACKAGES_PATH`.

Pinned versions:

- EDK2: `edk2-stable202608`
- LVGL: `v9.6.0` (git submodule at `Library/LvglLib/lvgl`)

Clone with submodules:

```sh
git clone --recursive https://github.com/weinet000210/LVGLUefiGuiPkg.git
# or, in an existing clone
git submodule update --init
```

EDK2 resolves packages by directory name, so the repository must be checked
out (or symlinked) as a directory named `LVGLUefiGuiPkg`.

## Linux (toolchain tag `GCC`)

edk2-stable202608 removed the `GCC5` tag; use `GCC`.

1. Install tools (Debian/Ubuntu):

   ```sh
   sudo apt-get install build-essential nasm uuid-dev python3 git
   # optional, for running in QEMU:
   sudo apt-get install qemu-system-x86 ovmf
   ```

   On a non-x86 host also install `gcc-x86-64-linux-gnu` and export
   `GCC_BIN=x86_64-linux-gnu-`.

2. Get edk2 and build BaseTools:

   ```sh
   git clone -b edk2-stable202608 https://github.com/tianocore/edk2.git
   git -C edk2 submodule update --init --depth 1 \
     BaseTools/Source/C/BrotliCompress/brotli \
     MdePkg/Library/MipiSysTLib/mipisyst
   make -C edk2/BaseTools -j"$(nproc)"
   ```

3. Get the package:

   ```sh
   git clone --recursive https://github.com/weinet000210/LVGLUefiGuiPkg.git
   ```

4. Build (any of DEBUG, RELEASE, NOOPT):

   ```sh
   EDK2_DIR=$PWD/edk2 LVGLUefiGuiPkg/scripts/build.sh DEBUG RELEASE
   ```

   Output: `Build-ws/Build/LVGLUefiGuiPkg/<TARGET>_GCC/X64/*.efi`.
   Set `WORKSPACE` to place the build output elsewhere.

### Manual build without the script

```sh
export WORKSPACE=$PWD/Build-ws
export PACKAGES_PATH=$PWD/edk2:$PWD          # directory that contains LVGLUefiGuiPkg/
. edk2/edksetup.sh
build -p LVGLUefiGuiPkg/LVGLUefiGuiPkg.dsc -a X64 -t GCC -b DEBUG
```

## Windows (toolchain tag `VS2022`)

1. Install Visual Studio 2022 (Desktop development with C++), Python 3.12,
   Git and NASM 2.15.05+ (`choco install nasm`).

2. Get edk2 and build BaseTools from a "x86 Native Tools Command Prompt for
   VS 2022" (BaseTools default to 32-bit host binaries in `BaseTools\Bin\Win32`;
   building them with the x64 compiler fails with C4311/C4312 errors):

   ```bat
   git clone -b edk2-stable202608 https://github.com/tianocore/edk2.git
   cd edk2
   git submodule update --init --depth 1 BaseTools/Source/C/BrotliCompress/brotli MdePkg/Library/MipiSysTLib/mipisyst
   set PYTHON_COMMAND=python
   edksetup.bat Rebuild VS2022
   cd ..
   ```

3. Build:

   ```bat
   git clone --recursive https://github.com/weinet000210/LVGLUefiGuiPkg.git
   set EDK2_DIR=%CD%\edk2
   set NASM_PREFIX=C:\Program Files\NASM\
   LVGLUefiGuiPkg\scripts\build.bat DEBUG
   ```

   Output: `Build-ws\Build\LVGLUefiGuiPkg\<TARGET>_VS2022\X64\*.efi`.

## Using the libraries from another package

Add the library instances to your platform DSC:

```ini
[LibraryClasses]
  UefiGuiRuntimeLib|LVGLUefiGuiPkg/Library/UefiGuiRuntimeLib/UefiGuiRuntimeLib.inf
  UefiGuiDisplayLib|LVGLUefiGuiPkg/Library/UefiGuiDisplayLib/UefiGuiDisplayLib.inf
  UefiGuiInputLib|LVGLUefiGuiPkg/Library/UefiGuiInputLib/UefiGuiInputLib.inf
  UefiGuiLib|LVGLUefiGuiPkg/Library/UefiGuiLib/UefiGuiLib.inf
  LvglLib|LVGLUefiGuiPkg/Library/LvglLib/LvglLib.inf
  CompilerIntrinsicsLib|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
```

and in the application INF:

```ini
[Packages]
  MdePkg/MdePkg.dec
  LVGLUefiGuiPkg/LVGLUefiGuiPkg.dec

[LibraryClasses]
  UefiGuiLib
  LvglLib

[BuildOptions]
  # LVGL headers include <stdint.h>; use the compiler's own, not libc's.
  GCC:*_*_*_CC_FLAGS = -ffreestanding
```

A minimal application:

```c
#include <Library/UefiGuiLib.h>
#include <Library/LvglLib.h>

  UefiGuiInitialize (NULL);                      // LVGL + display
  lv_obj_t *Label = lv_label_create (lv_screen_active ());
  lv_label_set_text (Label, "Hello");
  UefiGuiRun ();                                 // until UefiGuiRequestExit()
  UefiGuiShutdown ();
```

Widgets that take focus (buttons, text areas, ...) join LVGL's default group
when they are created, so the keyboard works without extra code: Tab and
Shift+Tab move the focus, Enter activates, Escape reaches the focused widget
as `LV_KEY_ESC`. Shortcuts (Ctrl+S and the like) go to the handler set with
`UefiGuiSetKeyHandler()`; a right click arrives as the event code returned by
`UefiGuiGetRightClickEventCode()`.

Under the UEFI Shell, the Shell itself also listens for Ctrl+C (abort the
running script) and Ctrl+S (pause output). An application can use both keys,
but after Ctrl+S the Shell's next output after the application exits waits for
one key press, and after Ctrl+C a running `.nsh` script stops once the
application returns.

## Running

On real hardware: copy the `.efi` to a FAT USB drive, boot to the UEFI Shell,
then run it, for example `fs0:` and `UefiGuiHello.efi`.

In QEMU:

```sh
# interactive window
LVGLUefiGuiPkg/scripts/run-qemu.sh Build-ws/Build/LVGLUefiGuiPkg/DEBUG_GCC/X64/UefiGuiHello.efi
# headless pass/fail check, as CI runs it
LVGLUefiGuiPkg/scripts/run-qemu.sh Build-ws/Build/LVGLUefiGuiPkg/DEBUG_GCC/X64/UefiGuiHello.efi --smoke UEFIGUI-HELLO
# GopTest with a screenshot of its test screen
LVGLUefiGuiPkg/scripts/run-qemu.sh Build-ws/Build/LVGLUefiGuiPkg/RELEASE_GCC/X64/GopTest.efi \
  --smoke UEFIGUI-GOPTEST --screenshot goptest.png --after "GOPTEST: SCREEN READY"
```

InputTest runs unit checks with fake devices, then asks the host for real
key presses and mouse movement by printing `UEFIGUI-CMD <n> <commands>` on
the serial port; `--drive` makes `scripts/qemu-driver.py` perform them
through the QEMU monitor. OVMF has no USB mouse driver, so the MdeModulePkg
drivers built with this package are loaded from the shell first:

```sh
# keyboard + Simple Pointer mouse
LVGLUefiGuiPkg/scripts/run-qemu.sh Build-ws/Build/LVGLUefiGuiPkg/DEBUG_GCC/X64/InputTest.efi \
  --smoke UEFIGUI-INPUTTEST --drive --pointer mouse \
  --load Build-ws/Build/LVGLUefiGuiPkg/DEBUG_GCC/X64/UsbMouseDxe.efi
# keyboard + Absolute Pointer (UsbMouseAbsolutePointerDxe on the same mouse)
#   --load .../UsbMouseAbsolutePointerDxe.efi
# keyboard only
#   --pointer none
```

For interactive use with a mouse, load `UsbMouseDxe.efi` from the shell
(`load fs0:\UsbMouseDxe.efi`, then `connect -r`) before starting the
application.

LvglPortTest runs the LVGL port checks, shows its screen for 6 seconds and
prints the results (`--smoke UEFIGUI-LVGLPORT`, screenshot marker
`LVGLPORTTEST: SCREEN READY`).

RuntimeTest checks the event loop (Phase 5): idle sleep, LVGL timer and
animation timing, callbacks posted from a TPL_CALLBACK notification
function, application events, exit requests, re-entry guards, keyboard
wake-up, timers and typing together, and cleanup after five
init / shutdown cycles. `--cpu-ratio 3` also compares the CPU time of the
QEMU process during 3 s of idle GUI with 3 s of deliberate busy polling:

```sh
LVGLUefiGuiPkg/scripts/run-qemu.sh Build-ws/Build/LVGLUefiGuiPkg/DEBUG_GCC/X64/RuntimeTest.efi \
  --smoke UEFIGUI-RUNTIMETEST --drive --pointer mouse --cpu-ratio 3 \
  --load Build-ws/Build/LVGLUefiGuiPkg/DEBUG_GCC/X64/UsbMouseDxe.efi
```

GopTest shows its test screen for 6 seconds (or until a key press), then
prints the results. Run interactively, it is the quickest visual check of a
new machine's GOP.

`OVMF_CODE` and `OVMF_VARS` override the firmware paths.

### LVGL performance monitor

Off by default: with it compiled in, LVGL never pauses its display refresh
timer and an idle screen redraws at frame rate. To measure, add
`-DUEFI_GUI_PERF_MONITOR=1` to the compiler flags of every module that
includes LVGL (for example `GCC:*_*_*_CC_FLAGS = -DUEFI_GUI_PERF_MONITOR=1`
and `MSFT:*_*_*_CC_FLAGS = /DUEFI_GUI_PERF_MONITOR=1` in the DSC
`[BuildOptions]`), then set `Debug.ShowPerformanceMonitor = TRUE`.

## Continuous integration

`.github/workflows/build.yml` runs on every push and pull request:

- `linux-gcc`: builds DEBUG and RELEASE with `GCC` and runs UefiGuiHello,
  GopTest, LvglPortTest, InputTest (mouse, absolute pointer, keyboard
  only) and RuntimeTest (DEBUG and RELEASE, with the host CPU check) in
  QEMU/OVMF, failing unless each prints its `PASS` marker. Test
  screens are uploaded as the `screenshots` artifact.
- `windows-vs2022`: builds DEBUG and RELEASE with `VS2022`.

Both jobs upload the `.efi` files as artifacts.
