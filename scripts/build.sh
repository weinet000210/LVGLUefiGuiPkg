#!/usr/bin/env bash
# Build LVGLUefiGuiPkg with the EDK2 GCC toolchain (Linux).
#
#   EDK2_DIR=/path/to/edk2 scripts/build.sh [DEBUG|RELEASE|NOOPT ...]
#
# Requirements: edk2-stable202608 with BaseTools built (make -C BaseTools),
# GCC 5 or newer targeting x86_64 (native or x86_64-linux-gnu- cross),
# NASM 2.15.05+, python3.
# Optional env: GCC_BIN (tool prefix, e.g. x86_64-linux-gnu- on non-x86 hosts),
#               WORKSPACE (build output root, default: <repo>/../Build-ws).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
: "${EDK2_DIR:?set EDK2_DIR to your edk2 checkout}"
EDK2_DIR="$(cd "$EDK2_DIR" && pwd)"
TARGETS=("$@")
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(DEBUG)

# EDK2 resolves the package by its directory name, so expose the repo as
# LVGLUefiGuiPkg inside a directory that goes on PACKAGES_PATH.
if [ "$(basename "$REPO")" = "LVGLUefiGuiPkg" ]; then
  PKG_PARENT="$(dirname "$REPO")"
else
  PKG_PARENT="$(mktemp -d)"
  ln -s "$REPO" "$PKG_PARENT/LVGLUefiGuiPkg"
fi

export WORKSPACE="${WORKSPACE:-$(dirname "$REPO")/Build-ws}"
mkdir -p "$WORKSPACE"
export PACKAGES_PATH="$EDK2_DIR:$PKG_PARENT"
export EDK_TOOLS_PATH="$EDK2_DIR/BaseTools"

# Source edksetup.sh without this script's arguments (it parses "$@"), and
# with nounset relaxed (it reads unset variables).
edk2_setup() {
  set +u
  . ./edksetup.sh > /dev/null
  local rc=$?
  set -u
  return $rc
}
cd "$EDK2_DIR"
edk2_setup
cd "$WORKSPACE"

for T in "${TARGETS[@]}"; do
  build -p LVGLUefiGuiPkg/LVGLUefiGuiPkg.dsc -a X64 -t GCC -b "$T" -n "$(nproc)"
  echo "Output: $WORKSPACE/Build/LVGLUefiGuiPkg/${T}_GCC/X64/"
done
