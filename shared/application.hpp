// SPDX-License-Identifier: MIT
#pragma once
#include "protocol.hpp"
namespace telemetry {
constexpr uint32_t kDemoTicks = 30;
constexpr size_t kOutputDepth = 8;
struct Sink {
    void *context;
    bool (*sample)(void *, const Frame &);
    void (*control)(void *, const Frame &);
};
struct Application {
    uint32_t generated, sample_drops, command_errors;
    void acquire(uint32_t tick, uint32_t source, Sink sink);
    // Valid commands ACK with [command, result]; result 0=OK, 1=invalid semantics.
    // Stop returns true; the caller sends status and terminates after draining output.
    bool command(const Frame &frame, uint32_t tick, Sink sink);
    Frame status(uint32_t seq, const ParseStats &parser, uint32_t rx_loss, uint32_t timer_loss) const;
};
} // namespace telemetry
