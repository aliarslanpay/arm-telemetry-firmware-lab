// SPDX-License-Identifier: MIT
#include "application.hpp"
namespace telemetry {
void Application::acquire(uint32_t tick, uint32_t source, Sink sink) {
    Frame f{Sample, 12, ++generated, {}};
    put32(f.payload, tick); put32(f.payload + 4, (f.sequence * 17 + 23) % 1000); put32(f.payload + 8, source);
    if (!sink.sample(sink.context, f)) ++sample_drops;
}
bool Application::command(const Frame &f, uint32_t tick, Sink sink) {
    const bool valid = ((f.type == Ping || f.type == Stop) && f.length == 0) ||
                       (f.type == Burst && f.length == 1 && f.payload[0] >= 1 && f.payload[0] <= 32);
    Frame ack{Ack, 2, f.sequence, {f.type, static_cast<uint8_t>(!valid)}};
    if (!valid) ++command_errors;
    else if (f.type == Burst) for (unsigned i = 0; i < f.payload[0]; ++i) acquire(tick, 1, sink);
    sink.control(sink.context, ack);
    return valid && f.type == Stop;
}
Frame Application::status(uint32_t seq, const ParseStats &p, uint32_t rx_loss, uint32_t timer_loss) const {
    Frame f{Status, 32, seq, {}};
    const uint32_t words[8] = {p.accepted, p.crc_errors, p.length_errors,
        p.malformed + p.version_errors + command_errors, rx_loss, generated, sample_drops, timer_loss};
    for (unsigned i = 0; i < 8; ++i) put32(f.payload + 4 * i, words[i]);
    return f;
}
} // namespace telemetry
