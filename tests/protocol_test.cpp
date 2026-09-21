// SPDX-License-Identifier: MIT
#include "protocol.hpp"
#include "bounded_queue.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace telemetry;
static std::vector<uint8_t> raw_wire(std::vector<uint8_t> body) {
    const auto crc = crc16(body.data(), body.size()); body.push_back(crc & 255); body.push_back(crc >> 8);
    std::vector<uint8_t> wire{kFlag};
    for (auto byte : body) {
        if (byte == kFlag || byte == kEscape) { wire.push_back(kEscape); wire.push_back(byte ^ 0x20); }
        else wire.push_back(byte);
    }
    wire.push_back(kFlag); return wire;
}
static unsigned feed(Parser &p, const std::vector<uint8_t> &wire, Frame &out) {
    unsigned n = 0; for (auto b : wire) n += p.feed(b, out); return n;
}
int main() {
    assert(crc16(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0x29b1);
    Frame f{Ping, kPayload, 0x7e7d0001, {}}, out{};
    for (unsigned i = 0; i < kPayload; ++i) f.payload[i] = i % 2 ? kFlag : kEscape;
    uint8_t encoded[kWire]; const auto length = encode(f, encoded, sizeof encoded);
    assert(length && length <= kWire);
    assert(!encode(f, encoded, length - 1));
    std::vector<uint8_t> wire(encoded, encoded + length);
    // Every split point, including directly after an escape, retains parser state.
    for (size_t split = 0; split <= wire.size(); ++split) {
        Parser p{}; unsigned received = 0;
        for (size_t i = 0; i < split; ++i) received += p.feed(wire[i], out);
        for (size_t i = split; i < wire.size(); ++i) received += p.feed(wire[i], out);
        assert(received == 1 && out.sequence == f.sequence && out.length == kPayload);
        assert(!memcmp(out.payload, f.payload, kPayload));
    }
    Parser p{};
    auto corrupt = raw_wire({1, Ping, 0, 0, 9, 0, 0, 0}); corrupt[5] ^= 1;
    assert(!feed(p, corrupt, out) && p.stats.crc_errors == 1);
    assert(feed(p, wire, out) == 1);
    assert(!feed(p, raw_wire({1, Ping, 33, 0, 0, 0, 0, 0}), out));
    assert(p.stats.length_errors == 1);
    assert(!feed(p, raw_wire({2, Ping, 0, 0, 0, 0, 0, 0}), out));
    assert(p.stats.version_errors == 1);
    assert(!feed(p, {kFlag, 1, Ping, 0, kFlag}, out) && p.stats.malformed == 1);
    assert(!feed(p, {kFlag, 1, kEscape, kFlag}, out) && p.stats.malformed == 2);
    auto excessive = std::vector<uint8_t>{1, Ping, 32, 0, 0, 0, 0, 0}; excessive.resize(200, 0);
    assert(!feed(p, raw_wire(excessive), out) && p.stats.length_errors == 2);
    assert(feed(p, wire, out) == 1);
    for (auto b : {kFlag, uint8_t{1}, uint8_t{1}}) p.feed(b, out);
    p.reset_transport(); assert(feed(p, wire, out) == 1);
    // Exercise arbitrary byte streams under sanitizers, then a known frame recovers.
    uint32_t seed = 1;
    for (unsigned i = 0; i < 100000; ++i) { seed = seed * 1664525 + 1013904223; p.feed(seed >> 24, out); }
    assert(feed(p, wire, out) == 1);
    BoundedQueue<unsigned, 8> q{}; unsigned value = 0;
    for (unsigned i = 0; i < 8; ++i) assert(q.push(i));
    assert(!q.push(99));
    for (unsigned i = 0; i < 4; ++i) { assert(q.pop(value) && value == i); assert(q.push(i + 8)); }
    for (unsigned i = 4; i < 12; ++i) assert(q.pop(value) && value == i);
    assert(!q.pop(value));
    f.length = 33; assert(!encode(f, encoded, sizeof encoded));
    puts("PASS protocol: CRC vector, all fragment splits, limits, corruption, recovery, bounded FIFO");
}
