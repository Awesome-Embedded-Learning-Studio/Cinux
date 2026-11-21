#!/usr/bin/env python3
# The shared QEMU boot harness for the guest-side test scripts: boots an
# image, streams the serial log, and offers typed input and QMP control.
# The self-host kernel-build test used to own this class; the native
# and persistence tests share it now.
import pathlib
import select
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "scripts" / "mcp"))
from qemu_args import device_args, qemu_command


class Guest:
    def __init__(self, image, volume, log, timeout, memory=512, monitor=None, ps2_only=False,
                 gui=False, qmp=None):
        subprocess.run(['cmake', '--build', str(image.parent.parent), '--target', 'qemu_disks'],
                       check=True, stdout=subprocess.DEVNULL)
        devices = device_args(image, 'qemu-gui-args.txt' if gui else 'qemu-device-args.txt')
        if gui:
            for index, argument in enumerate(devices):
                if argument == '-display':
                    devices[index + 1] = 'none'
            devices = [word for index, word in enumerate(devices)
                       if word != '-m' and (index == 0 or devices[index - 1] != '-m')]
        if ps2_only:
            devices = [word for index, word in enumerate(devices)
                       if not (word.startswith(('usb-kbd', 'usb-tablet'))
                               or (word == '-device' and index + 1 < len(devices)
                                   and devices[index + 1].startswith(('usb-kbd', 'usb-tablet'))))]
        for index, argument in enumerate(devices):
            if argument.startswith("format=raw,file=") and argument.endswith(",if=none,id=spare"):
                devices[index] = f"format=raw,file={volume},if=none,id=spare"
        self.log = log
        self.timeout = timeout
        self.transcript = bytearray()
        self.process = subprocess.Popen(
            [*qemu_command(image), *devices, "-m", f"{memory}M", "-boot", "order=c",
             "-drive", f"format=raw,file={image},if=none,id=boot",
             "-device", "ide-hd,drive=boot,bus=ide.0,bootindex=0",
             *(["-qmp", f"unix:{qmp},server,nowait"] if qmp else []),
             "-serial", "stdio", "-monitor", f"unix:{monitor},server,nowait" if monitor else "none"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def wait_for(self, pattern, start=0):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            match = re.search(pattern, self.transcript[start:])
            if match:
                return match
            if b"[kern] exception" in self.transcript[start:]:
                raise RuntimeError("kernel or user exception observed; see serial log")
            if self.process.poll() is not None:
                raise RuntimeError(f"QEMU exited: {self.process.returncode}")
            if select.select([self.process.stdout], [], [], 0.1)[0]:
                self.transcript.extend(os.read(self.process.stdout.fileno(), 65536))
                self.log.write_bytes(self.transcript)
        raise RuntimeError(f"marker timed out: {pattern!r}")

    def command(self, text, marker):
        start = len(self.transcript)
        self.process.stdin.write((text + "\n").encode())
        self.process.stdin.flush()
        match = self.wait_for(rb"\r?\n" + marker.encode() + rb"_(\d+)\r?\n", start)
        if match[1] != b"0":
            raise RuntimeError(f"command failed ({match[1].decode()}): {text}")
        self.wait_for_prompt(start + match.end())
        return bytes(self.transcript[start:])

    def wait_for_prompt(self, start=0):
        return self.wait_for(rb"(?:/[^\r\n]*|~[^\r\n]*) [#$] ", start)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.log.write_bytes(self.transcript)

