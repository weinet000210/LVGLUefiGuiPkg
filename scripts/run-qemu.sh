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
# --drive (smoke mode only): run scripts/qemu-driver.py, which executes the
#   input commands the application requests on the serial console
#   ("UEFIGUI-CMD <seq> <cmd>[;<cmd>...]", cmd = sendkey | mouse_move |
#   mouse_button) through the QEMU monitor, like a person at the keyboard.
#   With --screenshot FILE, the application's "screenshot" command saves it.
# --load DRIVER.efi (repeatable): copy the driver to the ESP and load it from
#   the shell (then "connect -r") before the application starts.
# --pointer mouse|tablet|none: USB pointer device (default: tablet).
# --cpu-ratio N (with --drive): the application measures host CPU time with
#   "cpu begin" / "cpu end idle" and "cpu end busy"; fail unless the QEMU
#   process used at least N times more CPU in the busy period than in the
#   idle one.
#
# Env: OVMF_CODE, OVMF_VARS (default: /usr/share/OVMF/OVMF_*_4M.fd),
#      QEMU (default: qemu-system-x86_64), QEMU_TIMEOUT (smoke, default 120).
set -euo pipefail

APP="${1:?usage: run-qemu.sh <app.efi> [--smoke MARKER] [qemu args]}"
shift
SMOKE=""
SHOT=""
SHOT_AFTER=""
DRIVE=""
LOADS=()
POINTER="tablet"
CPU_RATIO=""
while [ $# -gt 0 ]; do
  case "$1" in
    --smoke) SMOKE="${2:?--smoke needs a marker}"; shift 2 ;;
    --screenshot) SHOT="${2:?--screenshot needs a file}"; shift 2 ;;
    --after) SHOT_AFTER="${2:?--after needs a text}"; shift 2 ;;
    --drive) DRIVE=1; shift ;;
    --load) LOADS+=("${2:?--load needs a driver}"); shift 2 ;;
    --pointer) POINTER="${2:?--pointer needs mouse, tablet or none}"; shift 2 ;;
    --cpu-ratio) CPU_RATIO="${2:?--cpu-ratio needs a number}"; shift 2 ;;
    *) break ;;
  esac
done
if [ -n "$SHOT" ] && { [ -z "$SMOKE" ] || { [ -z "$SHOT_AFTER" ] && [ -z "$DRIVE" ]; }; }; then
  echo "--screenshot needs --smoke and --after (or --drive)" >&2
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
PRELOAD=""
for DRV in ${LOADS[@]+"${LOADS[@]}"}; do
  cp "$DRV" "$TMP/esp/"
  PRELOAD+="load fs0:\\$(basename "$DRV")"$'\r\n'
done
[ -n "$PRELOAD" ] && PRELOAD+="connect -r"$'\r\n'
case "$POINTER" in
  mouse)  POINTER_DEV=(-device usb-mouse) ;;
  tablet) POINTER_DEV=(-device usb-tablet) ;;
  none)   POINTER_DEV=() ;;
  *) echo "--pointer: mouse, tablet or none" >&2; exit 2 ;;
esac
cp "$OVMF_VARS" "$TMP/vars.fd"

COMMON=(-machine q35 -m 512
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE"
  -drive if=pflash,format=raw,file="$TMP/vars.fd"
  -drive file=fat:rw:"$TMP/esp",format=raw
  -device VGA,xres=1024,yres=768
  -device qemu-xhci -device usb-kbd ${POINTER_DEV[@]+"${POINTER_DEV[@]}"}
  -net none)

if [ -z "$SMOKE" ]; then
  printf 'fs0:\r\n%s%s\r\n' "$PRELOAD" "$NAME" > "$TMP/esp/startup.nsh"
  "$QEMU" "${COMMON[@]}" "$@"
  exit $?
fi

printf 'fs0:\r\n%s%s\r\nreset -s\r\n' "$PRELOAD" "$NAME" > "$TMP/esp/startup.nsh"
: > "$TMP/serial.log"
timeout "${QEMU_TIMEOUT:-120}" "$QEMU" "${COMMON[@]}" -display none -no-reboot \
  -serial file:"$TMP/serial.log" -monitor unix:"$TMP/monitor.sock",server,nowait "$@" &
QEMU_PID=$!
DRIVER_PID=""
# Power off a few seconds after the verdict appears, in case the shell script
# does not reach "reset -s" (for example after the app saw Ctrl+C, which the
# UEFI Shell also takes as "abort the script").
( for _ in $(seq 1 "${QEMU_TIMEOUT:-120}"); do
    if grep -aq "$SMOKE: \(PASS\|FAIL\)" "$TMP/serial.log" 2>/dev/null; then
      sleep 3; kill "$QEMU_PID" 2>/dev/null; break
    fi
    kill -0 "$QEMU_PID" 2>/dev/null || break
    sleep 1
  done ) &
if [ -n "$DRIVE" ]; then
  if [ -n "$SHOT" ]; then
    export DRIVER_SHOT="$(cd "$(dirname "$SHOT")" && pwd)/$(basename "$SHOT")"
  fi
  export DRIVER_CPU_LOG="$TMP/host-cpu.txt"
  python3 "$(dirname "$0")/qemu-driver.py" "$TMP/serial.log" "$TMP/monitor.sock" "$QEMU_PID" &
  DRIVER_PID=$!
fi

if [ -n "$SHOT" ] && [ -z "$DRIVE" ]; then
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
[ -n "$DRIVER_PID" ] && wait "$DRIVER_PID" || true
if [ -n "$DRIVE" ] && [ -n "$SHOT" ]; then
  [ -s "$SHOT" ] && echo "screenshot saved: $SHOT" || echo "screenshot FAILED" >&2
fi
# Strip terminal escape sequences and carriage returns from the console log.
sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$TMP/serial.log" | tr -d '\r' > "$TMP/console.txt"
sed -n "/^FS0:\\\\> $NAME/,\$p" "$TMP/console.txt"
if ! grep -q "$SMOKE: PASS" "$TMP/console.txt"; then
  echo "smoke test FAILED (no '$SMOKE: PASS' on the console)" >&2
  exit 1
fi
if [ -n "$CPU_RATIO" ]; then
  IDLE="$(awk '$1=="idle"{print $2}' "$TMP/host-cpu.txt" 2>/dev/null | tail -n1)"
  BUSY="$(awk '$1=="busy"{print $2}' "$TMP/host-cpu.txt" 2>/dev/null | tail -n1)"
  echo "host CPU of the QEMU process: idle GUI ${IDLE:-?}%, busy-polling reference ${BUSY:-?}%"
  if [ -z "$IDLE" ] || [ -z "$BUSY" ] || \
     ! awk -v i="$IDLE" -v b="$BUSY" -v r="$CPU_RATIO" 'BEGIN { exit !(b > 0 && b >= r * i) }'; then
    echo "host CPU check FAILED (busy must be at least ${CPU_RATIO}x idle)" >&2
    exit 1
  fi
fi
echo "smoke test passed"
