// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include "protocol.hpp"
namespace board {
constexpr uint32_t kClockHz = 25000000;
struct InputStats { uint32_t dropped, discarded, hardware_overruns, interrupts; };
uint32_t irq_save();
void irq_restore(uint32_t mask);
void uart_init();
void enable_input();
void send(const telemetry::Frame &frame);
void text(const char *p);
bool input_pop(uint8_t &byte);
bool input_recover();
InputStats input_stats();
void stop(uint32_t status);
void idle();
} // namespace board
extern "C" void input_wake_from_isr();
