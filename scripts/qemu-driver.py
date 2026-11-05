#!/usr/bin/env python3
## @file
#  Drive a QEMU guest's keyboard and mouse on request of the application.
#
#  The application under test writes lines of the form
#      UEFIGUI-CMD <seq> <cmd>[;<cmd>...]
#  to the serial console. Each new <seq> is executed once, command by
#  command, through the QEMU human monitor, with a short pause between
#  commands (like keys typed by a person). Only input commands are allowed:
#  sendkey, mouse_move, mouse_button, plus "screenshot", which saves the
#  screen to $DRIVER_SHOT (PNG) when that is set.
#
#  Usage: qemu-driver.py <serial.log> <monitor.sock> <qemu-pid>
#  Env:   DRIVER_CMD_DELAY (seconds between commands, default 0.04)
#
#  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

import os
import re
import socket
import sys
import time

ALLOWED = ("sendkey", "mouse_move", "mouse_button")
LINE = re.compile(rb"UEFIGUI-CMD (\d+) ([^\r\n]*)\r?\n")


def monitor(sock_path, command):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    s.settimeout(2)
    try:
        buf = b""
        while b"(qemu)" not in buf:
            buf += s.recv(4096)
        s.sendall(command.encode() + b"\n")
        buf = b""
        while buf.count(b"(qemu)") < 1:
            buf += s.recv(4096)
    except socket.timeout:
        pass
    finally:
        s.close()


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def main():
    log, sock, pid = sys.argv[1], sys.argv[2], int(sys.argv[3])
    delay = float(os.environ.get("DRIVER_CMD_DELAY", "0.04"))
    done = set()
    while alive(pid):
        try:
            with open(log, "rb") as fh:
                data = fh.read()
        except OSError:
            data = b""
        for match in LINE.finditer(data):
            seq = int(match.group(1))
            if seq in done:
                continue
            done.add(seq)
            for cmd in match.group(2).decode("ascii", "replace").split(";"):
                cmd = cmd.strip()
                if not cmd:
                    continue
                if cmd == "screenshot":
                    shot = os.environ.get("DRIVER_SHOT")
                    if shot:
                        monitor(sock, "screendump %s -f png" % shot)
                        time.sleep(1.0)
                    continue
                if cmd.split()[0] not in ALLOWED:
                    print("qemu-driver: refused '%s'" % cmd, file=sys.stderr)
                    continue
                monitor(sock, cmd)
                time.sleep(delay)
            print("qemu-driver: step %d done" % seq, file=sys.stderr)
        time.sleep(0.05)


if __name__ == "__main__":
    main()
