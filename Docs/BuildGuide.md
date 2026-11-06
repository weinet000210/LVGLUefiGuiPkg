# Build Guide

LVGLUefiGuiPkg builds as an ordinary EDK2 package. No file inside the edk2
tree is modified; the package is found through `PACKAGES_PATH`.

Pinned versions:

- EDK2: `edk2-stable202608`
- LVGL: `v9.6.0` (git submodule, added in Phase 2)

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
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
```

and in the application INF:

```ini
[Packages]
  MdePkg/MdePkg.dec
  LVGLUefiGuiPkg/LVGLUefiGuiPkg.dec

[LibraryClasses]
  UefiGuiRuntimeLib
  UefiGuiDisplayLib
```

The remaining library classes get instances in later phases; this section
will list them as they land.

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

GopTest shows its test screen for 6 seconds (or until a key press), then
prints the results. Run interactively, it is the quickest visual check of a
new machine's GOP.

`OVMF_CODE` and `OVMF_VARS` override the firmware paths.

## Continuous integration

`.github/workflows/build.yml` runs on every push and pull request:

- `linux-gcc`: builds DEBUG and RELEASE with `GCC` and runs UefiGuiHello and
  GopTest in QEMU/OVMF, failing unless each prints its `PASS` marker. The
  GopTest screen is uploaded as the `screenshots` artifact.
- `windows-vs2022`: builds DEBUG and RELEASE with `VS2022`.

Both jobs upload the `.efi` files as artifacts.
