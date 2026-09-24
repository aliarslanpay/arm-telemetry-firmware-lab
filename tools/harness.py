#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""QEMU UART harness. Every wait and guest lifetime has a deadline."""
import argparse
import os
import pathlib
import select
import subprocess
import time
import struct
from wire import Frame, Decoder, encode, PING, BURST, STOP, SAMPLE, ACK, STATUS
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

def ack(guest, seq, command):
    frame = guest.wait_frame(lambda f: f.kind == ACK and f.sequence == seq)
    assert frame.payload == bytes([command, 0]), frame

def stop(guest, seq):
    guest.send(Frame(STOP, seq))
    ack(guest, seq, STOP)
    status = guest.wait_frame(lambda f: f.kind == STATUS and f.sequence == seq)
    assert len(status.payload) == 32
    assert guest.process.wait(timeout=3) == 0
    return struct.unpack("<8I", status.payload)

def smoke(image):
    with Guest(image) as guest:
        guest.send(Frame(PING, 17)); ack(guest, 17, PING)
        stats = stop(guest, 18)
        assert stats[0] == 2 and not any(stats[1:5])
    print(f"PASS {image.name}: UART IRQ round-trip; clean stop")

def auto_demo(image):
    with Guest(image) as guest:
        status = guest.wait_frame(lambda f: f.kind == STATUS, timeout=6)
        samples = [f for f in guest.frames if f.kind == SAMPLE]
        stats = struct.unpack("<8I", status.payload)
        assert [f.sequence for f in samples] == list(range(1, 31))
        assert [struct.unpack("<3I", f.payload)[0] for f in samples] == list(range(1, 31))
        assert all(struct.unpack("<3I", f.payload)[1:] == ((f.sequence * 17 + 23) % 1000, 0) for f in samples)
        assert stats == (0, 0, 0, 0, 0, 30, 0, 0), stats
        assert guest.process.wait(timeout=3) == 0
    print(f"PASS {image.name}: bounded automatic demo, 30 timer samples, no loss")

def overload(image):
    with Guest(image) as guest:
        guest.send(Frame(BURST, 20, bytes([32]))); ack(guest, 20, BURST)
        burst = [f for f in guest.frames if f.kind == SAMPLE and struct.unpack("<3I", f.payload)[2] == 1]
        assert len(burst) == 8, burst
        assert [f.sequence for f in burst] == list(range(burst[0].sequence, burst[0].sequence + 8)), burst
        guest.send(Frame(PING, 21)); ack(guest, 21, PING)
        stats = stop(guest, 22)
        assert stats[6] == 24 and not any(stats[1:5]) and stats[7] == 0, stats
    print(f"PASS {image.name}: 32-sample burst retains first 8, drops newest 24, continues")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=pathlib.Path, default=ROOT / "build/baremetal.elf")
    parser.add_argument("--scenario", choices=["smoke", "auto", "overload", "all"], default="all")
    args = parser.parse_args()
    scenarios = {"smoke": smoke, "auto": auto_demo, "overload": overload}
    for name, test in scenarios.items():
        if args.scenario in (name, "all"):
            test(args.image)
