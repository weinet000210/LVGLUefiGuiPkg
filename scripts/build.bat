@echo off
rem Build LVGLUefiGuiPkg with Visual Studio 2022 (Windows).
rem
rem   set EDK2_DIR=C:\src\edk2
rem   scripts\build.bat [DEBUG|RELEASE|NOOPT]
rem
rem Run from any prompt after BaseTools are built (see Docs/BuildGuide.md);
rem edksetup.bat locates VS2022 itself. NASM must be installed and NASM_PREFIX
rem set (for example C:\Program Files\NASM\). The repository directory must
rem be named LVGLUefiGuiPkg.
setlocal
if "%EDK2_DIR%"=="" (
  echo Set EDK2_DIR to your edk2 checkout.
  exit /b 1
)
set TARGET=%1
if "%TARGET%"=="" set TARGET=DEBUG

for %%I in ("%~dp0..") do set REPO=%%~fI
for %%I in ("%REPO%\..") do set PKG_PARENT=%%~fI
for %%I in ("%REPO%") do set REPO_NAME=%%~nxI
if /I not "%REPO_NAME%"=="LVGLUefiGuiPkg" (
  echo The repository directory must be named LVGLUefiGuiPkg, found %REPO_NAME%.
  exit /b 1
)

if "%WORKSPACE%"=="" set WORKSPACE=%PKG_PARENT%\Build-ws
if not exist "%WORKSPACE%" mkdir "%WORKSPACE%"
set PACKAGES_PATH=%EDK2_DIR%;%PKG_PARENT%
set EDK_TOOLS_PATH=%EDK2_DIR%\BaseTools

pushd "%EDK2_DIR%"
call edksetup.bat VS2022
if errorlevel 1 exit /b 1
popd

pushd "%WORKSPACE%"
call build -p LVGLUefiGuiPkg\LVGLUefiGuiPkg.dsc -a X64 -t VS2022 -b %TARGET%
set RC=%ERRORLEVEL%
popd
if %RC% neq 0 exit /b %RC%
echo Output: %WORKSPACE%\Build\LVGLUefiGuiPkg\%TARGET%_VS2022\X64\
