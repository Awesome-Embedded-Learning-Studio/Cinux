#!/usr/bin/env python3
"""Exercise run_gui's USB keyboard without sending any serial input."""

import argparse
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


class Qmp:
    def __init__(self, process):
        # QMP over inherited pipes also works in sandboxes that deny bind().
        self.stream = process.stdout
        self.writer = process.stdin
        json.loads(self.stream.readline())
        self.call("qmp_capabilities")

    def call(self, command, **arguments):
        request = {"execute": command, "arguments": arguments}
        self.writer.write(json.dumps(request).encode() + b"\n")
        self.writer.flush()
        while True:
            reply = json.loads(self.stream.readline())
            if "error" in reply:
                raise RuntimeError(reply["error"])
            if "return" in reply:
                return reply["return"]

    def key(self, code, down):
        self.call("input-send-event", events=[{
            "type": "key", "data": {
                "down": down, "key": {"type": "qcode", "data": code},
            },
        }])
        time.sleep(0.04)

    def press(self, code):
        self.key(code, True)
        self.key(code, False)

    def text(self, value):
        plain = {" ": "spc", "/": "slash", "\n": "ret"}
        shifted = {">": "dot"}
        for char in value:
            shift = char.isupper() or char in shifted
            code = shifted.get(char, plain.get(char, char.lower()))
            if shift:
                self.key("shift", True)
            self.press(code)
            if shift:
                self.key("shift", False)

    def terminal(self):
        self.call("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": 40 * 32767 // 640}},
            {"type": "abs", "data": {"axis": "y", "value": 110 * 32767 // 480}},
        ])
        time.sleep(0.3)
        for _ in range(2):
            for down in (True, False):
                self.call("input-send-event", events=[{
                    "type": "btn", "data": {"down": down, "button": "left"},
                }])
                time.sleep(0.15)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-armv7"))
    parser.add_argument("--qemu", default="qemu-system-arm")
    args = parser.parse_args()
    build = args.build.resolve()
    log = build / "armv7-gui-keyboard.serial.log"
    before = build / "armv7-gui-keyboard-before.ppm"
    after = build / "armv7-gui-keyboard-after.ppm"
    with tempfile.TemporaryDirectory(prefix="cinux-gui-keys-") as temporary:
        temporary = Path(temporary)
        image = build / "boot" / "native-build.ext2"
        with log.open("wb") as serial:
            process = subprocess.Popen([
                args.qemu, "-M", "mcimx6ul-evk",
                "-kernel", str(build / "kernel/arch/armv7/arm.elf"),
                "-device", f"loader,file={build / 'armv7-init-gui.dtb'},addr=0x81000000,force-raw=on",
                # snapshot=on: /scratch writes stay in this session's
                # overlay, so parallel boots share the card read-only.
                "-drive", f"if=sd,format=raw,file={image},snapshot=on",
                "-display", "none", "-serial", f"file:{log}", "-monitor", "none",
                "-qmp", "stdio",
                "-device", "usb-kbd,bus=usb-bus.0",
                "-device", "usb-tablet,bus=usb-bus.1",
            ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=serial,
                env=dict(os.environ, TMPDIR=str(build)))
            try:
                deadline = time.monotonic() + 90
                while b"vesper-fbdev: running" not in log.read_bytes():
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(f"desktop did not start; see {log}")
                    time.sleep(0.2)
                qmp = Qmp(process)
                qmp.terminal()
                time.sleep(1)
                qmp.call("screendump", filename=str(before))
                # Ctrl-U, Shift, backspace, cursor movement and Enter must all
                # survive HID -> event0 -> desktop -> PTY -> shell.
                qmp.text("discard")
                qmp.key("ctrl", True)
                qmp.press("u")
                qmp.key("ctrl", False)
                qmp.text("echo GuiKey2x")
                qmp.press("backspace")
                qmp.press("left")
                qmp.text("4")
                qmp.press("right")
                qmp.text(" > /scratch\n")
                time.sleep(0.5)
                qmp.text("cat /scratch\n")
                time.sleep(1)
                qmp.call("screendump", filename=str(after))
                # Read back through the shell to serial output as an exact
                # witness; the guest's write-back cache need not be flushed.
                qmp.text("cat /scratch > /dev/console\n")
                deadline = time.monotonic() + 5
                while b"GuiKey42\n" not in log.read_bytes().replace(b"\r", b""):
                    if time.monotonic() > deadline:
                        raise RuntimeError(f"USB keyboard command did not answer; see {log}")
                    time.sleep(0.1)
                qmp.call("quit")
                process.wait(timeout=10)
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)

        if b"echo Gui" in log.read_bytes() or b"cat /scratch" in log.read_bytes():
            raise RuntimeError("grabbed GUI keyboard leaked characters to the console shell")
        old = before.read_bytes().split(b"\n", 3)[3]
        new = after.read_bytes().split(b"\n", 3)[3]
        changed = sum(old[i:i + 3] != new[i:i + 3] for i in range(0, len(old), 3))
        if changed < 400:
            raise RuntimeError(f"terminal did not draw the typed text ({changed} changed pixels)")
        print(f"USB GUI keyboard ok: GuiKey42, Ctrl-U, backspace, arrows, shell and redraw ({changed} pixels)")
        print(f"screen: {after}")


if __name__ == "__main__":
    main()
