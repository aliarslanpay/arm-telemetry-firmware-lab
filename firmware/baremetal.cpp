// SPDX-License-Identifier: MIT
#include "platform.hpp"
using namespace telemetry;
extern "C" void firmware_main() {
    board::uart_init(); board::enable_input();
    board::text("READY baremetal\n");
    Parser parser{}; Frame frame{};
    for (;;) {
        if (board::input_recover()) parser.reset_transport();
        uint8_t byte;
        auto mask = board::irq_save();
        const bool available = board::input_pop(byte);
        if (!available) board::idle();
        board::irq_restore(mask);
        if (!available) continue;
        if (parser.feed(byte, frame)) {
            if (frame.type == Stop) { frame.type = Ack; board::send(frame); board::stop(0); }
            frame.type = Ack; board::send(frame);
        }
    }
}
