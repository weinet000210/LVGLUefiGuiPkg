#!/usr/bin/env bash
# Run a UefiGui application in QEMU + OVMF from the UEFI Shell.
#
#   scripts/run-qemu.sh <app.efi> [--smoke MARKER] [extra qemu args]
#
# Interactive (default): opens a QEMU window with USB keyboard and mouse.
# --smoke MARKER: headless run; the shell runs the app, then powers off. The
#   script fails unless "MARKER: PASS" appears on the serial console.
#
# Env: OVMF_CODE, OVMF_VARS (default: /usr/share/OVMF/OVMF_*_4M.fd),
#      QEMU (default: qemu-system-x86_64), QEMU_TIMEOUT (smoke, default 120).
set -euo pipefail

APP="${1:?usage: run-qemu.sh <app.efi> [--smoke MARKER] [qemu args]}"
shift
SMOKE=""
if [ "${1:-}" = "--smoke" ]; then
  SMOKE="${2:?--smoke needs a marker}"
  shift 2
fi

OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS="${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
QEMU="${QEMU:-qemu-system-x86_64}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/esp"
cp "$APP" "$TMP/esp/"
NAME="$(basename "$APP")"
cp "$OVMF_VARS" "$TMP/vars.fd"

COMMON=(-machine q35 -m 512
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE"
  -drive if=pflash,format=raw,file="$TMP/vars.fd"
  -drive file=fat:rw:"$TMP/esp",format=raw
  -device VGA,xres=1024,yres=768
  -device qemu-xhci -device usb-kbd -device usb-tablet
  -net none)

if [ -z "$SMOKE" ]; then
  printf 'fs0:\r\n%s\r\n' "$NAME" > "$TMP/esp/startup.nsh"
  "$QEMU" "${COMMON[@]}" "$@"
  exit $?
fi

printf 'fs0:\r\n%s\r\nreset -s\r\n' "$NAME" > "$TMP/esp/startup.nsh"
timeout "${QEMU_TIMEOUT:-120}" "$QEMU" "${COMMON[@]}" -display none -no-reboot \
  -serial file:"$TMP/serial.log" "$@" || true
# Strip terminal escape sequences and carriage returns from the console log.
sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$TMP/serial.log" | tr -d '\r' > "$TMP/console.txt"
sed -n "/^FS0:\\\\> $NAME/,\$p" "$TMP/console.txt"
if grep -q "$SMOKE: PASS" "$TMP/console.txt"; then
  echo "smoke test passed"
else
  echo "smoke test FAILED (no '$SMOKE: PASS' on the console)" >&2
  exit 1
fi
