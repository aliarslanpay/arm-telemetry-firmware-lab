// SPDX-License-Identifier: MIT
#include "protocol.hpp"
namespace telemetry {
void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i); }
uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) {
    uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i); return v;
}
uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= static_cast<uint16_t>(*p++) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}
size_t encode(const Frame &f, uint8_t *wire, size_t capacity) {
    if (f.length > kPayload) return 0;
    uint8_t raw[kBody] = {};
    raw[0] = kVersion; raw[1] = f.type; put16(raw + 2, f.length); put32(raw + 4, f.sequence);
    for (size_t i = 0; i < f.length; ++i) raw[8 + i] = f.payload[i];
    const size_t n = 8 + f.length;
    put16(raw + n, crc16(raw, n));
    size_t needed = 2;
    for (size_t i = 0; i < n + 2; ++i) needed += (raw[i] == kFlag || raw[i] == kEscape) ? 2 : 1;
    if (capacity < needed) return 0;
    size_t at = 0; wire[at++] = kFlag;
    for (size_t i = 0; i < n + 2; ++i) {
        if (raw[i] == kFlag || raw[i] == kEscape) { wire[at++] = kEscape; wire[at++] = raw[i] ^ 0x20; }
        else wire[at++] = raw[i];
    }
    wire[at++] = kFlag; return at;
}
void Parser::reset_transport() { used = 0; collecting = escaped = discard = false; }
bool Parser::feed(uint8_t byte, Frame &f) {
    if (byte == kFlag) {
        bool valid = false;
        if (collecting && !discard && (used || escaped)) {
            if (escaped || used < 10) ++stats.malformed;
            else if (get16(body + 2) > kPayload || used != static_cast<size_t>(10 + get16(body + 2))) ++stats.length_errors;
            else if (get16(body + used - 2) != crc16(body, used - 2)) ++stats.crc_errors;
            else {
                f.type = body[1]; f.length = get16(body + 2); f.sequence = get32(body + 4);
                for (size_t i = 0; i < f.length; ++i) f.payload[i] = body[8 + i];
                ++stats.accepted; valid = true;
            }
        }
        used = 0; collecting = true; escaped = discard = false; return valid;
    }
    if (!collecting || discard) return false;
    if (escaped) { byte ^= 0x20; escaped = false; }
    else if (byte == kEscape) { escaped = true; return false; }
    if (used == kBody) { ++stats.length_errors; discard = true; return false; }
    body[used++] = byte;
    if (used == 1 && body[0] != kVersion) { ++stats.version_errors; discard = true; }
    if (used == 4 && get16(body + 2) > kPayload) { ++stats.length_errors; discard = true; }
    return false;
}
} // namespace telemetry
