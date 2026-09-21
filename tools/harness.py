#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""QEMU UART harness. Every wait and guest lifetime has a deadline."""
import argparse
import os
import pathlib
import select
import subprocess
import time
from wire import Frame, Decoder, encode, PING, STOP, ACK
ROOT = pathlib.Path(__file__).resolve().parents[1]

class Guest:
    def __init__(self, image):
        self.process = subprocess.Popen([
            os.environ.get("QEMU", "qemu-system-arm"), "-M", "mps2-an385",
            "-display", "none", "-monitor", "none",
            "-chardev", "stdio,id=uart,signal=off,mux=on", "-serial", "chardev:uart",
            "-semihosting-config", "enable=on,target=native", "-kernel", str(image),
        ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.decoder = Decoder()
        self.frames = []
        self.preamble = bytearray()
        self.ready = False

    def read(self, timeout):
        readers, _, _ = select.select([self.process.stdout], [], [], max(0, timeout))
        if not readers:
            return
        data = os.read(self.process.stdout.fileno(), 4096)
        if not data:
            raise RuntimeError(f"guest exited early: {self.process.poll()}, {self.process.stderr.read().decode()}")
        if not self.ready:
            self.preamble.extend(data)
            if b"\n" not in self.preamble:
                return
            line, data = bytes(self.preamble).split(b"\n", 1)
            if not line.startswith(b"READY "):
                raise AssertionError(f"unexpected boot: {line!r}")
            self.ready = True
        self.frames.extend(self.decoder.feed(data))

    def wait_ready(self):
        deadline = time.monotonic() + 5
        while not self.ready and time.monotonic() < deadline:
            self.read(deadline - time.monotonic())
        assert self.ready, "boot deadline exceeded"

    def send_bytes(self, data):
        # QEMU stdio mux buffers input; its Ctrl-A escape must be doubled.
        # This transport adapter does not alter the bytes delivered to UART0.
        self.process.stdin.write(data.replace(b"\x01", b"\x01\x01"))
        self.process.stdin.flush()

    def send(self, frame):
        self.send_bytes(encode(frame))

    def wait_frame(self, predicate, timeout=3):
        deadline = time.monotonic() + timeout
        while True:
            for i, frame in enumerate(self.frames):
                if predicate(frame):
                    return self.frames.pop(i)
            if time.monotonic() >= deadline:
                raise AssertionError("frame deadline exceeded")
            self.read(deadline - time.monotonic())

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()

    def __enter__(self):
        try:
            self.wait_ready()
        except BaseException:
            self.close()
            raise
        return self

    def __exit__(self, *_):
        self.close()

def smoke(image):
    with Guest(image) as guest:
        guest.send(Frame(PING, 17))
        assert guest.wait_frame(lambda f: f.kind == ACK and f.sequence == 17).payload == b""
        guest.send(Frame(STOP, 18))
        guest.wait_frame(lambda f: f.kind == ACK and f.sequence == 18)
        assert guest.process.wait(timeout=3) == 0
    print("PASS baremetal: UART ISR -> bounded byte ring -> parser -> ACK; clean stop")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=pathlib.Path, default=ROOT / "build/baremetal.elf")
    smoke(parser.parse_args().image)
