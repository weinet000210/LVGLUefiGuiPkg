#!/usr/bin/env python3
"""Turn PerfTest console output into Markdown tables.

    scripts/perf-report.py console.txt [--label "QEMU TCG, GitHub runner"]

Reads the "UEFIGUI-PERF <key> <value> <unit>" lines PerfTest prints and
writes Markdown to stdout (used for Docs/Performance.md and the GitHub
Actions job summary). Times are printed in milliseconds with two decimals.
"""
import re
import sys

LINE = re.compile(r"UEFIGUI-PERF (\S+) (\d+) (\S+)")


def load(path):
    metrics = {}
    with open(path, "rb") as fh:
        for raw in fh.read().decode("utf-8", "replace").splitlines():
            m = LINE.search(raw)
            if m:
                metrics[m.group(1)] = (int(m.group(2)), m.group(3))
    return metrics


def ms(metrics, key):
    if key not in metrics:
        return "n/a"
    value, unit = metrics[key]
    if unit == "us":
        return "%.2f" % (value / 1000.0)
    return "%d %s" % (value, unit)


def raw(metrics, key, suffix=""):
    if key not in metrics:
        return "n/a"
    return "%d%s" % (metrics[key][0], suffix)


def kib(metrics, key):
    if key not in metrics:
        return "n/a"
    return "%.1f" % (metrics[key][0] / 1024.0)


def timing_row(metrics, name, prefix):
    return "| %s | %s | %s | %s | %s | %s |" % (
        name, ms(metrics, prefix + ".min"), ms(metrics, prefix + ".median"),
        ms(metrics, prefix + ".p95"), ms(metrics, prefix + ".max"),
        raw(metrics, prefix + ".samples"))


HEAD = "| Measurement | min (ms) | median (ms) | p95 (ms) | max (ms) | samples |\n|---|---:|---:|---:|---:|---:|"


def main():
    args = sys.argv[1:]
    label = None
    if "--label" in args:
        i = args.index("--label")
        label = args[i + 1]
        del args[i:i + 2]
    m = load(args[0])
    if not m:
        print("No UEFIGUI-PERF lines found.")
        return 1
    out = []
    if label:
        out.append("Platform: %s. Timer frequency %s Hz; start mode %sx%s; GOP pixel format %s; %s GOP modes.\n" % (
            label, raw(m, "platform.time_frequency"), raw(m, "platform.gop.start_width"),
            raw(m, "platform.gop.start_height"), raw(m, "platform.gop.pixel_format"),
            raw(m, "platform.gop.mode_count")))

    out.append("### GOP Blt latency (raw GOP, %sx%s)\n" % (raw(m, "platform.gop.start_width"), raw(m, "platform.gop.start_height")))
    out.append(HEAD)
    for size in ("16x16", "64x64", "256x256", "band64", "full"):
        out.append(timing_row(m, "BufferToVideo " + size, "gop.buffer_to_video." + size))
    out.append(timing_row(m, "VideoFill full screen", "gop.video_fill.full"))
    out.append(timing_row(m, "VideoToBltBuffer 64x64", "gop.video_to_buffer.64x64"))
    out.append(timing_row(m, "VideoToBltBuffer full screen", "gop.video_to_buffer.full"))
    out.append("")

    out.append("### Display backends (UefiGuiDisplayFlushRect, XRGB8888 source)\n")
    out.append("FrameBuffer backend available: %s.\n" % ("yes" if m.get("display.framebuffer.supported", (0,))[0] else "no"))
    out.append(HEAD)
    for tag in ("blt", "framebuffer"):
        for size in ("16x16", "64x64", "256x256", "band64", "full"):
            key = "display.%s.xrgb8888.%s" % (tag, size)
            if key + ".median" in m:
                out.append(timing_row(m, "%s %s" % (tag, size), key))
        key = "display.%s.rgb565.full" % tag
        if key + ".median" in m:
            out.append(timing_row(m, "%s full, RGB565 source" % tag, key))
    out.append("")

    out.append("### LVGL full-screen update (test screen, lv_refr_now)\n")
    out.append("| Configuration | total median (ms) | render median (ms) | flush median (ms) | total max (ms) |\n|---|---:|---:|---:|---:|")
    for tag, name in (("blt.lines64", "Blt, 64-line buffer (default)"), ("blt.lines16", "Blt, 16-line buffer"),
                      ("blt.fullheight", "Blt, full-height buffer"), ("framebuffer.lines64", "FrameBuffer, 64-line buffer")):
        p = "lvgl.full." + tag
        if p + ".total.median" in m:
            out.append("| %s | %s | %s | %s | %s |" % (name, ms(m, p + ".total.median"), ms(m, p + ".render.median"),
                                                     ms(m, p + ".flush.median"), ms(m, p + ".total.max")))
    out.append("")

    out.append("### LVGL partial update (default configuration)\n")
    out.append("| Change | total median (ms) | total p95 (ms) | flush median (ms) | pixels (median) |\n|---|---:|---:|---:|---:|")
    for tag, name in (("button_label", "Button label text"), ("textarea_char", "Character typed into a text area"), ("bar_value", "Bar value")):
        p = "lvgl.partial." + tag
        out.append("| %s | %s | %s | %s | %s |" % (name, ms(m, p + ".total.median"), ms(m, p + ".total.p95"),
                                                 ms(m, p + ".flush.median"), raw(m, p + ".pixels.median")))
    out.append("")

    out.append("### Input response latency\n")
    out.append(HEAD)
    out.append(timing_row(m, "Key: event to LVGL handler", "input.key.handler"))
    out.append(timing_row(m, "Key: event to refreshed screen", "input.key.pixels"))
    out.append(timing_row(m, "Click (idle screen): event to handler", "input.click.handler"))
    out.append(timing_row(m, "Click (idle screen): event to refreshed screen", "input.click.pixels"))
    out.append(timing_row(m, "USB keyboard via host (includes host latency)", "input.host_usb_key.request_to_handler"))
    out.append("")

    out.append("### Memory (bytes allocated through UefiGuiAllocate)\n")
    out.append("| Item | KiB |\n|---|---:|")
    out.append("| After UefiGuiInitialize (default, %sx%s) | %s |" % (raw(m, "platform.gop.start_width"), raw(m, "platform.gop.start_height"), kib(m, "memory.gui.blt.lines64.after_init")))
    out.append("| With the test screen | %s |" % kib(m, "memory.gui.blt.lines64.with_scene"))
    out.append("| Peak of the default session | %s |" % kib(m, "memory.gui.peak_default_session"))
    out.append("| LVGL draw buffer (64 lines) | %s |" % kib(m, "memory.draw_buffer"))
    out.append("| Blt conversion buffer (64 lines) | %s |" % kib(m, "memory.blt_conversion_buffer"))
    out.append("| Full-height draw buffer session, with screen | %s |" % kib(m, "memory.gui.blt.fullheight.with_scene"))
    out.append("| One button with a label | %s |" % kib(m, "memory.gui.per_button_with_label"))
    out.append("| UEFI conventional memory used by the session (KiB) | %s |" % raw(m, "memory.system.used_by_session"))
    out.append("")

    out.append("### Resolutions (same screen, default configuration)\n")
    out.append("| Mode | full update median (ms) | render (ms) | flush (ms) | raw GOP full Blt (ms) | partial (label) median (ms) | memory with screen (KiB) |\n|---|---:|---:|---:|---:|---:|---:|")
    for res in ("640x480", "800x600", "1024x768", "1280x1024", "1920x1080", "2560x1600"):
        p = "res." + res
        if m.get(p + ".available", (0,))[0] != 1:
            out.append("| %s | not offered by the GOP | | | | | |" % res)
            continue
        out.append("| %s | %s | %s | %s | %s | %s | %s |" % (res, ms(m, p + ".full.total.median"), ms(m, p + ".full.render.median"),
                                                          ms(m, p + ".full.flush.median"), ms(m, p + ".gop_full_blt.median"),
                                                          ms(m, p + ".partial_label.total.median"), kib(m, p + ".memory_with_scene")))
    out.append("")

    out.append("### Soak\n")
    out.append("| Item | Value |\n|---|---:|")
    for key, name in (("soak.seconds", "Duration (s)"), ("soak.frames", "Frames rendered"), ("soak.flushed_mpixels", "Pixels flushed (M)"),
                      ("soak.churn_cycles", "Widget churn cycles"), ("soak.objects_created", "Widgets created and deleted"),
                      ("soak.injected_keys", "Keys injected"), ("soak.delivered_keys", "Keys delivered"), ("soak.inserted_chars", "Characters typed into the text area"),
                      ("soak.injected_moves", "Pointer moves injected"), ("soak.flush_errors", "Flush errors"),
                      ("soak.loop_busy_percent", "Loop busy time (%)"), ("soak.loop_longest_iteration", "Longest loop iteration (us)"),
                      ("soak.memory_samples", "Memory samples"), ("soak.memory_first_sample", "Memory at first sample (B)"),
                      ("soak.memory_last_sample", "Memory at last sample (B)"), ("soak.memory_growth", "Memory growth (B)")):
        out.append("| %s | %s |" % (name, raw(m, key)))
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
