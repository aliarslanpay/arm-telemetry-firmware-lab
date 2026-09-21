#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent Python implementation of the UART wire format."""
import binascii
import dataclasses
import struct
FLAG, ESCAPE, MAX_PAYLOAD = 0x7E, 0x7D, 32
PING, BURST, STOP, SAMPLE, ACK, STATUS = 1, 2, 3, 0x81, 0x82, 0x83

@dataclasses.dataclass(frozen=True)
class Frame:
    kind: int
    sequence: int
    payload: bytes = b""

def wrap(body):
    wire = bytearray([FLAG])
    for byte in body:
        if byte in (FLAG, ESCAPE):
            wire.extend([ESCAPE, byte ^ 0x20])
        else:
            wire.append(byte)
    wire.append(FLAG)
    return bytes(wire)

def encode(frame):
    if len(frame.payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds 32 bytes")
    body = struct.pack("<BBHI", 1, frame.kind, len(frame.payload), frame.sequence) + frame.payload
    return wrap(body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF)))

class Decoder:
    def __init__(self):
        self.body = bytearray()
        self.started = self.escaped = False

    def feed(self, data):
        result = []
        for byte in data:
            if byte == FLAG:
                if self.started and self.body:
                    if self.escaped or len(self.body) < 10:
                        raise ValueError("malformed guest frame")
                    version, kind, length, seq = struct.unpack("<BBHI", self.body[:8])
                    if version != 1 or length > MAX_PAYLOAD or len(self.body) != length + 10:
                        raise ValueError("invalid guest header")
                    if binascii.crc_hqx(self.body[:-2], 0xFFFF) != struct.unpack("<H", self.body[-2:])[0]:
                        raise ValueError("guest CRC mismatch")
                    result.append(Frame(kind, seq, bytes(self.body[8:-2])))
                self.started, self.escaped = True, False
                self.body.clear()
            elif self.started:
                if self.escaped:
                    self.body.append(byte ^ 0x20)
                    self.escaped = False
                elif byte == ESCAPE:
                    self.escaped = True
                else:
                    self.body.append(byte)
                if len(self.body) > 42:
                    raise ValueError("guest frame exceeds buffer")
        return result
