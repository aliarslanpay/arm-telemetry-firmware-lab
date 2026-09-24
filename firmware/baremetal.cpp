// SPDX-License-Identifier: MIT
#include "platform.hpp"
#include "application.hpp"
#include "bounded_queue.hpp"
using namespace telemetry;
namespace {
BoundedQueue<Frame, kOutputDepth> output{};
bool enqueue_sample(void *, const Frame &f) { return output.push(f); }
void drain() { Frame f{}; while (output.pop(f)) board::send(f); }
void enqueue_control(void *, const Frame &f) {
    // Preserve control responses: make room by completing previously accepted output.
    if (output.count == kOutputDepth) drain();
    (void)output.push(f);
}
}
extern "C" void firmware_main() {
    board::uart_init(); board::enable_input(); board::timer_start(10);
    board::text("READY baremetal\n");
    Parser parser{}; Application app{}; Frame frame{};
    Sink sink{nullptr, enqueue_sample, enqueue_control};
    bool finish = false; uint32_t status_sequence = 0;
    while (!finish) {
        if (board::input_recover()) parser.reset_transport();
        uint8_t byte;
        // A finite byte budget prevents sustained input from starving acquisition.
        for (unsigned n = 0; n < 128 && board::input_pop(byte); ++n) {
            if (parser.feed(byte, frame) && app.command(frame, board::timer_ticks(), sink)) {
                finish = true; status_sequence = frame.sequence; break;
            }
        }
        uint32_t tick;
        while (!finish && board::timer_pop(tick)) {
            app.acquire(tick, 0, sink);
            finish = tick >= kDemoTicks;
        }
        drain();
        if (!finish) {
            // Check and sleep under PRIMASK to close the check-before-WFI race.
            // A pending IRQ wakes WFI even though exception entry is masked.
            auto mask = board::irq_save();
            if (!board::work_pending()) board::idle();
            board::irq_restore(mask);
        }
    }
    board::timer_stop();
    auto input = board::input_stats();
    board::send(app.status(status_sequence, parser.stats,
        input.dropped + input.discarded + input.hardware_overruns, board::timer_drops()));
    board::stop(0);
}
