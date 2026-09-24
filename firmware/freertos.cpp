// SPDX-License-Identifier: MIT
#include "platform.hpp"
#include "application.hpp"
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
}
using namespace telemetry;
namespace {
enum EventKind : uint32_t { Byte, Tick, Loss };
struct InputEvent { EventKind kind; uint32_t value; };
struct Output { Frame frame; bool finish; };
constexpr unsigned kInputDepth = 64;
StaticTask_t acquire_tcb, process_tcb, output_tcb;
StackType_t acquire_stack[256], process_stack[512], output_stack[384];
TaskHandle_t acquire_handle, process_handle, output_handle;
StaticQueue_t input_tcb, output_queue_tcb;
uint8_t input_storage[kInputDepth * sizeof(InputEvent)];
uint8_t output_storage[kOutputDepth * sizeof(Output)];
QueueHandle_t input_queue, output_queue;
uint32_t lost_event_bytes, lost_event_ticks;

void send_event(const InputEvent &event) {
    if (xQueueSend(input_queue, &event, 0) == pdPASS) return;
    // The single producer runs above the consumer. Flush a damaged event stream,
    // count its discarded data, and insert a parser-reset marker before future input.
    InputEvent old = event;
    do {
        if (old.kind == Byte) ++lost_event_bytes;
        else if (old.kind == Tick) ++lost_event_ticks;
    } while (xQueueReceive(input_queue, &old, 0) == pdPASS);
    const InputEvent reset{Loss, 0};
    configASSERT(xQueueSend(input_queue, &reset, 0) == pdPASS);
}
void acquisition(void *) {
    board::enable_input(); board::timer_start(10);
    // Written before input can be processed; output task is still blocked.
    board::text("READY freertos\n");
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (board::input_recover()) send_event({Loss, 0});
        uint8_t byte;
        for (unsigned n = 0; n < 128 && board::input_pop(byte); ++n) send_event({Byte, byte});
        uint32_t tick;
        while (board::timer_pop(tick)) send_event({Tick, tick});
    }
}
bool enqueue_sample(void *, const Frame &frame) {
    const Output item{frame, false};
    return xQueueSend(output_queue, &item, 0) == pdPASS;
}
void enqueue_control(void *, const Frame &frame) {
    const Output item{frame, false};
    configASSERT(xQueueSend(output_queue, &item, pdMS_TO_TICKS(1000)) == pdPASS);
}
void finalize(Application &app, Parser &parser, uint32_t seq) {
    board::timer_stop();
    vTaskSuspend(acquire_handle); // Stable loss counters while constructing status.
    auto input = board::input_stats();
    Frame resources{0x84, 16, seq, {}};
    put32(resources.payload, uxTaskGetStackHighWaterMark(acquire_handle));
    put32(resources.payload + 4, uxTaskGetStackHighWaterMark(process_handle));
    put32(resources.payload + 8, uxTaskGetStackHighWaterMark(output_handle));
    put32(resources.payload + 12, uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle()));
    enqueue_control(nullptr, resources);
    enqueue_control(nullptr, app.status(seq, parser.stats,
        input.dropped + input.discarded + input.hardware_overruns + lost_event_bytes,
        board::timer_drops() + lost_event_ticks));
    const Output exit_item{{}, true};
    configASSERT(xQueueSend(output_queue, &exit_item, pdMS_TO_TICKS(1000)) == pdPASS);
    vTaskSuspend(nullptr);
    for (;;) {}
}
void processing(void *) {
    Parser parser{}; Application app{}; Frame frame{};
    Sink sink{nullptr, enqueue_sample, enqueue_control};
    InputEvent event{};
    for (;;) {
        configASSERT(xQueueReceive(input_queue, &event, portMAX_DELAY) == pdPASS);
        if (event.kind == Loss) parser.reset_transport();
        else if (event.kind == Byte) {
            if (parser.feed(event.value, frame) && app.command(frame, board::timer_ticks(), sink))
                finalize(app, parser, frame.sequence);
        } else {
            app.acquire(event.value, 0, sink);
            if (event.value >= kDemoTicks) finalize(app, parser, 0);
        }
    }
}
void output(void *) {
    Output item{};
    for (;;) {
        configASSERT(xQueueReceive(output_queue, &item, portMAX_DELAY) == pdPASS);
        if (item.finish) board::stop(0);
        board::send(item.frame);
    }
}
void notify() {
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(acquire_handle, &wake);
    portYIELD_FROM_ISR(wake);
}
}
extern "C" void input_wake_from_isr() { notify(); }
extern "C" void timer_wake_from_isr() { notify(); }
extern "C" void project_panic(const char *message) { board::text(message); board::stop(3); }
extern "C" void firmware_main() {
    board::uart_init();
    // PRIGROUP 0: maximum preemption bits. Port validates the implemented
    // priority-bit count, grouping, and syscall threshold, including the 8-bit case.
    *reinterpret_cast<volatile uint32_t *>(0xe000ed0c) = 0x05fa0000;
    input_queue = xQueueCreateStatic(kInputDepth, sizeof(InputEvent), input_storage, &input_tcb);
    output_queue = xQueueCreateStatic(kOutputDepth, sizeof(Output), output_storage, &output_queue_tcb);
    acquire_handle = xTaskCreateStatic(acquisition, "acquire", 256, nullptr, 3, acquire_stack, &acquire_tcb);
    process_handle = xTaskCreateStatic(processing, "process", 512, nullptr, 2, process_stack, &process_tcb);
    output_handle = xTaskCreateStatic(output, "output", 384, nullptr, 1, output_stack, &output_tcb);
    configASSERT(input_queue && output_queue && acquire_handle && process_handle && output_handle);
    vTaskStartScheduler();
    project_panic("SCHEDULER_RETURN\n");
}
