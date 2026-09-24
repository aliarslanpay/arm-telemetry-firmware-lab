// SPDX-License-Identifier: MIT
#include "platform.hpp"
#include "bounded_queue.hpp"
namespace {
volatile uint32_t *const uart = reinterpret_cast<volatile uint32_t *>(0x40004000);
volatile uint32_t *const timer = reinterpret_cast<volatile uint32_t *>(0x40000000);
telemetry::BoundedQueue<uint8_t, 128> rx{};
board::InputStats rx_stats{};
bool lost{};
telemetry::BoundedQueue<uint32_t, 4> ticks{};
uint32_t timer_count{}, timer_lost{};
void put_byte(uint8_t b) { while (uart[1] & 1) {} uart[0] = b; }
}
namespace board {
uint32_t irq_save() {
    uint32_t mask;
    __asm volatile("mrs %0, primask\ncpsid i" : "=r"(mask) :: "memory");
    return mask;
}
void irq_restore(uint32_t mask) { __asm volatile("msr primask, %0" :: "r"(mask) : "memory"); }
void uart_init() { uart[2] = 0; uart[4] = kClockHz / 115200; uart[3] = 15; uart[1] = 12; uart[2] = 3; }
void enable_input() {
    // Numeric priority 0x80 is lower urgency than FreeRTOS syscall threshold 0x40.
    reinterpret_cast<volatile uint8_t *>(0xe000e400)[0] = 0x80;
    *reinterpret_cast<volatile uint32_t *>(0xe000e100) = 1;
    uart[2] = 3 | 8; // TX, RX, RX interrupt. Hardware overruns checked in RX ISR.
}
void text(const char *p) { while (*p) put_byte(static_cast<uint8_t>(*p++)); }
void send(const telemetry::Frame &f) {
    uint8_t bytes[telemetry::kWire];
    const size_t n = telemetry::encode(f, bytes, sizeof bytes);
    for (size_t i = 0; i < n; ++i) put_byte(bytes[i]);
}
bool input_pop(uint8_t &b) { auto mask = irq_save(); bool ok = rx.pop(b); irq_restore(mask); return ok; }
bool input_recover() {
    auto mask = irq_save(); const bool was_lost = lost;
    if (lost) { rx_stats.discarded += rx.count; rx.clear(); lost = false; }
    irq_restore(mask); return was_lost;
}
InputStats input_stats() { auto mask = irq_save(); auto s = rx_stats; irq_restore(mask); return s; }
void timer_start(uint32_t hz) {
    timer[0] = 0; timer[2] = kClockHz / hz; timer[3] = 1;
    reinterpret_cast<volatile uint8_t *>(0xe000e400)[8] = 0x80;
    *reinterpret_cast<volatile uint32_t *>(0xe000e100) = 1 << 8;
    timer[0] = 1 | 8; // Enable and interrupt, internal APB clock.
}
bool timer_pop(uint32_t &tick) { auto mask = irq_save(); bool ok = ticks.pop(tick); irq_restore(mask); return ok; }
uint32_t timer_ticks() { auto mask = irq_save(); auto value = timer_count; irq_restore(mask); return value; }
uint32_t timer_drops() { auto mask = irq_save(); auto value = timer_lost; irq_restore(mask); return value; }
bool work_pending() { auto mask = irq_save(); bool ready = rx.count || ticks.count || lost; irq_restore(mask); return ready; }
void timer_stop() { timer[0] = 0; }
void stop(uint32_t status) {
    timer_stop();
    const uint32_t args[2] = {0x20026, status};
    register uint32_t r0 __asm("r0") = 0x20;
    register const uint32_t *r1 __asm("r1") = args;
    __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    for (;;) __asm volatile("wfi");
}
void idle() { __asm volatile("dsb\nwfi" ::: "memory"); }
}
extern "C" void UART0_Handler() {
    ++rx_stats.interrupts;
    if (uart[1] & 8) { ++rx_stats.hardware_overruns; lost = true; uart[1] = 8; }
    // One byte per ISR. Clear before DATA read: QEMU's accept_input callback can
    // deliver the next byte synchronously and must retain its new RX interrupt.
    uart[3] = 2;
    if (uart[1] & 2) {
        const auto b = static_cast<uint8_t>(uart[0]);
        if (!rx.push(b)) { ++rx_stats.dropped; lost = true; }
    }
    input_wake_from_isr();
}
extern "C" __attribute__((weak)) void input_wake_from_isr() {}
extern "C" void platform_fault() { board::text("FAULT\n"); board::stop(2); }

extern "C" void Timer0_Handler() {
    timer[3] = 1;
    ++timer_count;
    if (!ticks.push(timer_count)) ++timer_lost;
    timer_wake_from_isr();
}
extern "C" __attribute__((weak)) void timer_wake_from_isr() {}
