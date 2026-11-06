# Performance

Measured numbers for LVGLUefiGuiPkg, produced by `Test/PerfTest` (Phase 6).
How to run PerfTest and turn its log into these tables is in
[BuildGuide.md](BuildGuide.md).

## Conditions and limits

- **Virtual machine only.** All numbers below come from QEMU 10.0 (TCG, no
  KVM) with OVMF from Debian trixie, 1024x768 unless stated, `-m 512`, on an
  arm64 host emulating X64. TCG translates every guest instruction, so the
  absolute times say nothing about real hardware. Use them to compare
  configurations measured in the same run, not as targets.
- **No hardware has been measured.** Run `PerfTest.efi` on the target machine
  before drawing conclusions for it (see [PortingGuide.md](PortingGuide.md)).
- Build: GCC 14, RELEASE. The DEBUG build gives the
  same numbers within a few percent (LVGL dominates and is optimized in both).
- Timing uses the TSC calibrated against `gBS->Stall()`. OVMF's timer tick is
  10 ms, which bounds how quickly a sleeping loop wakes for a timer, but not
  for input (input events wake the loop directly).
- CI repeats the run on every push (GitHub-hosted x86-64 runner, also TCG) and
  writes the same tables into the job summary and the `perf-results`
  artifact. Expect different absolute numbers there; the CI figures quoted
  below come from run 42058330948 (commit 3f4fe30, RELEASE).

## Findings

1. **Rendering, not the GOP, dominates a full-screen update.** At 1024x768 a
   full update takes about 93 ms, of which about 75 ms is LVGL drawing and
   about 19 ms is the flush. A raw full-screen `Blt()` costs about 5.8 ms.
2. **Typical interaction redraws are about 1 ms.** A label change, a typed
   character or a bar value redraws 2,000 to 9,000 pixels in 0.9 to 1.5 ms.
3. **Input reaches the screen in about 2 to 3 ms** (median) from the moment
   the event is queued; p95 is below 6 ms. A key typed on the host USB
   keyboard takes about 42 ms end to end, almost all of it in QEMU's monitor
   and the host-side driver.
4. **The two display backends are close; the frame buffer gains at most
   about 4%.** With the default XRGB8888 source, a full-screen flush took
   20.0 ms through the frame buffer against 17.6 ms through `Blt()` in the
   local run (about 13% slower), and 5.37 ms against 5.60 ms on the CI runner
   (about 4% faster). The whole LVGL update was slightly slower with the frame
   buffer in both (94.0 vs 93.0 ms locally, 41.3 vs 40.3 ms on CI). With an
   RGB565 source the frame buffer is about twice as slow in both (133.8 vs
   59.9 ms locally, 70.9 vs 37.3 ms on CI). In OVMF, `Blt()` is a memory copy
   inside the firmware, so direct writes have little to save. The Blt backend
   stays the default; whether direct access pays off on a given machine has
   to be measured there.
5. **XRGB8888 conversion fast path.** Converting one word per pixel instead of
   going through the generic encoder cut a full-screen Blt-backend flush from
   71.7 ms to 19.0 ms and the full LVGL update from 150.5 ms to 92.1 ms
   (DEBUG, same run conditions, before and after the change).
6. **Draw buffer size trades memory for render time.** 16 lines: 132 ms; 64
   lines (default, 256 KiB): 93 ms; full height (3 MiB plus conversion
   buffer): 82 ms.
7. **Memory is small and stable.** About 524 KiB after `UefiGuiInitialize()`
   at 1024x768, half of it the draw buffer and half the Blt conversion buffer;
   both scale with the screen width. A 600 s soak ended with zero growth and
   every allocation returned at shutdown.
8. **Time scales with pixel count.** From 640x480 to 2560x1600 (13.3 times
   the pixels) the full update goes from 44 ms to 344 ms; partial updates stay
   at about 1 ms at every resolution.

## Measurements (RELEASE, 1024x768)

### GOP Blt latency (raw GOP, 1024x768)

| Measurement | min (ms) | median (ms) | p95 (ms) | max (ms) | samples |
|---|---:|---:|---:|---:|---:|
| BufferToVideo 16x16 | 0.01 | 0.01 | 0.02 | 0.14 | 400 |
| BufferToVideo 64x64 | 0.04 | 0.05 | 0.14 | 0.54 | 400 |
| BufferToVideo 256x256 | 0.47 | 0.55 | 0.87 | 1.76 | 256 |
| BufferToVideo band64 | 0.44 | 0.46 | 0.67 | 3.18 | 302 |
| BufferToVideo full | 5.62 | 5.82 | 6.07 | 6.24 | 26 |
| VideoFill full screen | 5.10 | 5.26 | 5.59 | 6.09 | 29 |
| VideoToBltBuffer 64x64 | 0.04 | 0.04 | 0.05 | 0.26 | 400 |
| VideoToBltBuffer full screen | 5.56 | 5.73 | 6.25 | 7.19 | 26 |

### Display backends (UefiGuiDisplayFlushRect, XRGB8888 source)

FrameBuffer backend available: yes.

| Measurement | min (ms) | median (ms) | p95 (ms) | max (ms) | samples |
|---|---:|---:|---:|---:|---:|
| blt 16x16 | 0.01 | 0.01 | 0.01 | 0.32 | 400 |
| blt 64x64 | 0.10 | 0.10 | 0.12 | 0.38 | 400 |
| blt 256x256 | 1.51 | 1.57 | 1.83 | 1.92 | 94 |
| blt band64 | 1.40 | 1.43 | 1.64 | 1.72 | 104 |
| blt full | 17.43 | 17.64 | 19.01 | 19.01 | 10 |
| blt full, RGB565 source | 59.80 | 59.91 | 60.36 | 60.36 | 10 |
| framebuffer 16x16 | 0.01 | 0.01 | 0.01 | 0.14 | 400 |
| framebuffer 64x64 | 0.10 | 0.11 | 0.12 | 0.38 | 400 |
| framebuffer 256x256 | 1.57 | 1.62 | 1.84 | 1.87 | 91 |
| framebuffer band64 | 1.55 | 1.61 | 1.79 | 2.21 | 92 |
| framebuffer full | 19.86 | 20.00 | 20.22 | 20.22 | 10 |
| framebuffer full, RGB565 source | 132.03 | 133.76 | 135.32 | 135.32 | 10 |

### LVGL full-screen update (test screen, lv_refr_now)

| Configuration | total median (ms) | render median (ms) | flush median (ms) | total max (ms) |
|---|---:|---:|---:|---:|
| Blt, 64-line buffer (default) | 92.97 | 74.53 | 18.78 | 96.24 |
| Blt, 16-line buffer | 131.95 | 112.55 | 18.82 | 153.51 |
| Blt, full-height buffer | 81.53 | 62.49 | 18.95 | 87.10 |
| FrameBuffer, 64-line buffer | 94.00 | 74.17 | 19.93 | 95.94 |

### LVGL partial update (default configuration)

| Change | total median (ms) | total p95 (ms) | flush median (ms) | pixels (median) |
|---|---:|---:|---:|---:|
| Button label text | 0.95 | 1.28 | 0.05 | 1896 |
| Character typed into a text area | 1.06 | 1.46 | 0.20 | 9096 |
| Bar value | 1.45 | 1.68 | 0.11 | 5161 |

### Input response latency

| Measurement | min (ms) | median (ms) | p95 (ms) | max (ms) | samples |
|---|---:|---:|---:|---:|---:|
| Key: event to LVGL handler | 0.59 | 0.75 | 1.38 | 1.98 | 40 |
| Key: event to refreshed screen | 1.80 | 2.12 | 4.37 | 5.94 | 40 |
| Click (idle screen): event to handler | 0.50 | 0.60 | 0.84 | 2.14 | 40 |
| Click (idle screen): event to refreshed screen | 2.59 | 2.79 | 5.44 | 6.23 | 40 |
| USB keyboard via host (includes host latency) | 21.97 | 42.23 | 61.16 | 61.16 | 8 |

### Memory (bytes allocated through UefiGuiAllocate)

| Item | KiB |
|---|---:|
| After UefiGuiInitialize (default, 1024x768) | 523.8 |
| With the test screen | 538.5 |
| Peak of the default session | 597.1 |
| LVGL draw buffer (64 lines) | 256.0 |
| Blt conversion buffer (64 lines) | 256.0 |
| Full-height draw buffer session, with screen | 6170.5 |
| One button with a label | 0.5 |
| UEFI conventional memory used by the session (KiB) | 592 |

### Resolutions (same screen, default configuration)

| Mode | full update median (ms) | render (ms) | flush (ms) | raw GOP full Blt (ms) | partial (label) median (ms) | memory with screen (KiB) |
|---|---:|---:|---:|---:|---:|---:|
| 640x480 | 43.95 | 36.55 | 7.41 | 2.31 | 0.99 | 346.5 |
| 800x600 | 65.71 | 53.86 | 11.72 | 3.60 | 0.96 | 426.5 |
| 1024x768 | 98.31 | 76.21 | 19.59 | 5.81 | 0.94 | 538.5 |
| 1280x1024 | 139.00 | 107.26 | 32.43 | 9.67 | 0.94 | 666.5 |
| 1920x1080 | 207.99 | 159.61 | 49.71 | 15.70 | 0.95 | 986.5 |
| 2560x1600 | 344.05 | 247.49 | 97.37 | 29.91 | 1.05 | 1306.5 |

### Soak (RELEASE, 600 s)

The soak keeps a screen with a spinner, an endless bar animation, a chart, a
table and a text area busy. A 200 ms LVGL timer deletes and recreates 12
widgets, adds a chart point, updates a table cell and a status label, and
forces a full refresh every tenth cycle. A 40 ms `TPL_CALLBACK` timer injects
a key press (25 per second, typed into the text area) and, every third tick, a
random pointer move. Every 5 s the test empties the churn container and the
text area and samples frames, busy time and memory (`UEFIGUI-SOAK-TRACE`
lines), so a leak shows up as growth between samples.

| Item | Value |
|---|---:|
| Duration (s) | 600 |
| Frames rendered | 24,729 (42 fps at start, 42 fps at end) |
| Widget churn cycles | 2,956 |
| Loop iterations | 43,078 |
| Loop busy time (%) | 46 |
| Memory in use, every sample (B) | 552,473 |
| Memory growth (B) | 0 |
| Flush errors, refused or dropped events | 0 |
| Keys injected vs characters typed | equal (all delivered) |
| Correctness checks | 55 passed, 0 failed |

The 5 s samples stayed between 203 and 214 frames and 40 to 48% busy for the
whole run. The longest single loop iteration was 568 ms; it occurred once,
between 125 s and 155 s, while the per-interval frame count did not drop, so
it is most likely a stall of the emulator on the shared host rather than work
done by the loop. The same applies to the longer outliers seen in earlier
runs that shared the host with other QEMU instances (up to 4.2 s, with lower
frame rates); those runs are not used here.

## Reproducing

```bash
LVGLUefiGuiPkg/scripts/run-qemu.sh Build-ws/Build/LVGLUefiGuiPkg/RELEASE_GCC/X64/PerfTest.efi \
  --smoke UEFIGUI-PERFTEST --drive --pointer mouse --args "-soak 600" \
  --load Build-ws/Build/LVGLUefiGuiPkg/RELEASE_GCC/X64/UsbMouseDxe.efi | tee perf.txt
python3 LVGLUefiGuiPkg/scripts/perf-report.py perf.txt
```

On a real machine, copy `PerfTest.efi` to a USB stick and run
`PerfTest.efi -soak 600` from the UEFI Shell; the tables come from the serial
or console log the same way. The keyboard and pointer sections inject their
own events and need no input from the user.
