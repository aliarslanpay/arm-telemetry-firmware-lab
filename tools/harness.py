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
import binascii
from wire import Frame, Decoder, encode, wrap, PING, BURST, STOP, SAMPLE, ACK, STATUS
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
        if image.name == "freertos.elf":
            resources = guest.wait_frame(lambda f: f.kind == 0x84)
            watermarks = struct.unpack("<4I", resources.payload)
            assert all(0 < free <= budget for free, budget in zip(watermarks, (256, 512, 384, 128)))
            print("STACK unused_words acquisition,processing,output,idle=" + str(watermarks))
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

def faults(image):
    with Guest(image) as guest:
        fragment = encode(Frame(PING, 0x7E7D0100))
        for chunk in (fragment[:3], fragment[3:8], fragment[8:-1], fragment[-1:]):
            guest.send_bytes(chunk)
            time.sleep(0.002)
        ack(guest, 0x7E7D0100, PING)
        header = struct.pack("<BBHI", 1, PING, 0, 200)
        guest.send_bytes(wrap(header + struct.pack("<H", binascii.crc_hqx(header, 0xFFFF) ^ 1)))
        guest.send(Frame(PING, 201)); ack(guest, 201, PING)
        oversized = struct.pack("<BBHI", 1, PING, 33, 202) + bytes(33)
        guest.send_bytes(wrap(oversized + struct.pack("<H", binascii.crc_hqx(oversized, 0xFFFF))))
        guest.send(Frame(PING, 203)); ack(guest, 203, PING)
        guest.send_bytes(bytes([0x7E, 1, PING, 0, 0x7E]))
        guest.send(Frame(PING, 204)); ack(guest, 204, PING)
        version = struct.pack("<BBHI", 2, PING, 0, 2050)
        guest.send_bytes(wrap(version + struct.pack("<H", binascii.crc_hqx(version, 0xFFFF))))
        guest.send(Frame(PING, 205)); ack(guest, 205, PING)
        for kind, seq, payload in ((BURST, 206, bytes([33])), (0x44, 207, b"")):
            guest.send(Frame(kind, seq, payload))
            response = guest.wait_frame(lambda f: f.kind == ACK and f.sequence == seq)
            assert response.payload == bytes([kind, 1])
        assert not any(f.kind == ACK and f.sequence in (200, 202, 2050) for f in guest.frames)
        stats = stop(guest, 208)
        assert stats[:5] == (8, 1, 1, 4, 0) and stats[6:] == (0, 0), stats
    print(f"PASS {image.name}: fragmented escaped header; CRC/length/truncation/version/command faults recover; counters={stats}")

def inversion(image):
    command = [os.environ.get("QEMU", "qemu-system-arm"), "-M", "mps2-an385", "-display", "none",
               "-monitor", "none", "-serial", "stdio", "-semihosting-config", "enable=on,target=native", "-kernel", str(image)]
    result = subprocess.run(command, input=b"", capture_output=True, timeout=5, check=True)
    lines = result.stdout.decode().splitlines()
    expected = {
        "binary": ["LOW_LOCK", "HIGH_WAIT", "MEDIUM_RUN", "LOW_RUN priority=1", "HIGH_LOCK", "DONE"],
        "mutex": ["LOW_LOCK", "HIGH_WAIT", "LOW_RUN priority=3", "HIGH_LOCK", "MEDIUM_RUN", "DONE"],
    }
    for phase, events in expected.items():
        actual = [line.removeprefix(f"EVENT {phase} ") for line in lines if line.startswith(f"EVENT {phase} ")]
        assert actual == events, (phase, actual)
    watermarks = [int(line.split("=")[1]) for line in lines if line.startswith("WATERMARK ")]
    assert len(watermarks) == 4 and all(n > 0 for n in watermarks), lines
    assert lines[-1] == "EXPERIMENT_OK", lines
    print(result.stdout.decode(), end="")
    print("PASS inversion: both exact event orders and inherited low-task priority asserted")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=pathlib.Path, default=ROOT / "build/baremetal.elf")
    parser.add_argument("--scenario", choices=["smoke", "auto", "overload", "faults", "inversion", "all"], default="all")
    args = parser.parse_args()
    scenarios = {"smoke": smoke, "auto": auto_demo, "overload": overload, "faults": faults, "inversion": inversion}
    for name, test in scenarios.items():
        if args.scenario == name or (args.scenario == "all" and name != "inversion"):
            test(args.image)
