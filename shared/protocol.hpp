// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace telemetry {
constexpr uint8_t kVersion = 1, kFlag = 0x7e, kEscape = 0x7d;
constexpr size_t kPayload = 32, kBody = 8 + kPayload + 2, kWire = 2 + 2 * kBody;
enum Type : uint8_t { Ping = 1, Burst = 2, Stop = 3, Sample = 0x81, Ack = 0x82, Status = 0x83 };
struct Frame { uint8_t type; uint16_t length; uint32_t sequence; uint8_t payload[kPayload]; };
struct ParseStats { uint32_t accepted, crc_errors, length_errors, version_errors, malformed; };
uint16_t crc16(const uint8_t *p, size_t n);
void put16(uint8_t *p, uint16_t v);
void put32(uint8_t *p, uint32_t v);
uint16_t get16(const uint8_t *p);
uint32_t get32(const uint8_t *p);
// Returns 0 for invalid length or insufficient output capacity; never writes partial frames.
size_t encode(const Frame &frame, uint8_t *wire, size_t capacity);
struct Parser {
    uint8_t body[kBody];
    size_t used;
    bool collecting, escaped, discard;
    ParseStats stats;
    bool feed(uint8_t byte, Frame &frame);
    // Retains counters, abandons the incomplete frame after transport byte loss.
    void reset_transport();
};
} // namespace telemetry
