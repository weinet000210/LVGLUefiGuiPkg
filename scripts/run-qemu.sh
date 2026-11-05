#!/usr/bin/env bash
# Run a UefiGui application in QEMU + OVMF from the UEFI Shell.
#
#   scripts/run-qemu.sh <app.efi> [--smoke MARKER] [--screenshot FILE.png --after TEXT]
#                       [extra qemu args]
#
# Interactive (default): opens a QEMU window with USB keyboard and mouse.
# --smoke MARKER: headless run; the shell runs the app, then powers off. The
#   script fails unless "MARKER: PASS" appears on the serial console.
# --screenshot FILE --after TEXT (smoke mode only): once TEXT appears on the
#   serial console, wait SHOT_DELAY seconds (default 2) and save the screen
#   as PNG through the QEMU monitor.
#
# Env: OVMF_CODE, OVMF_VARS (default: /usr/share/OVMF/OVMF_*_4M.fd),
#      QEMU (default: qemu-system-x86_64), QEMU_TIMEOUT (smoke, default 120).
set -euo pipefail

APP="${1:?usage: run-qemu.sh <app.efi> [--smoke MARKER] [qemu args]}"
shift
SMOKE=""
SHOT=""
SHOT_AFTER=""
while [ $# -gt 0 ]; do
  case "$1" in
    --smoke) SMOKE="${2:?--smoke needs a marker}"; shift 2 ;;
    --screenshot) SHOT="${2:?--screenshot needs a file}"; shift 2 ;;
    --after) SHOT_AFTER="${2:?--after needs a text}"; shift 2 ;;
    *) break ;;
  esac
done
if [ -n "$SHOT" ] && { [ -z "$SMOKE" ] || [ -z "$SHOT_AFTER" ]; }; then
  echo "--screenshot needs --smoke and --after" >&2
  exit 2
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
: > "$TMP/serial.log"
timeout "${QEMU_TIMEOUT:-120}" "$QEMU" "${COMMON[@]}" -display none -no-reboot \
  -serial file:"$TMP/serial.log" -monitor unix:"$TMP/monitor.sock",server,nowait "$@" &
QEMU_PID=$!

if [ -n "$SHOT" ]; then
  SHOT_ABS="$(cd "$(dirname "$SHOT")" && pwd)/$(basename "$SHOT")"
  for _ in $(seq 1 "${QEMU_TIMEOUT:-120}"); do
    grep -q "$SHOT_AFTER" "$TMP/serial.log" 2>/dev/null && break
    kill -0 "$QEMU_PID" 2>/dev/null || break
    sleep 1
  done
  if grep -q "$SHOT_AFTER" "$TMP/serial.log"; then
    sleep "${SHOT_DELAY:-2}"
    python3 - "$TMP/monitor.sock" "$SHOT_ABS" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
s.settimeout(5)
time.sleep(0.3)
s.recv(4096)
s.sendall(("screendump %s -f png\n" % sys.argv[2]).encode())
time.sleep(1.5)
s.close()
PY
    [ -s "$SHOT_ABS" ] && echo "screenshot saved: $SHOT" || echo "screenshot FAILED" >&2
  else
    echo "screenshot skipped: '$SHOT_AFTER' never appeared" >&2
  fi
fi

wait "$QEMU_PID" || true
# Strip terminal escape sequences and carriage returns from the console log.
sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$TMP/serial.log" | tr -d '\r' > "$TMP/console.txt"
sed -n "/^FS0:\\\\> $NAME/,\$p" "$TMP/console.txt"
if grep -q "$SMOKE: PASS" "$TMP/console.txt"; then
  echo "smoke test passed"
else
  echo "smoke test FAILED (no '$SMOKE: PASS' on the console)" >&2
  exit 1
fi
