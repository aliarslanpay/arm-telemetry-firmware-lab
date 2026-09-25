// SPDX-License-Identifier: MIT
// Separate experiment image: no telemetry timer or UART input task is active.
#include "platform.hpp"
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
}
namespace {
StaticTask_t coordinator_tcb, low_tcb, medium_tcb, high_tcb;
StackType_t coordinator_stack[384], low_stack[256], medium_stack[256], high_stack[256];
TaskHandle_t coordinator_handle, low_handle, medium_handle, high_handle;
StaticSemaphore_t binary_tcb, mutex_tcb;
SemaphoreHandle_t binary, mutex, active_lock;
bool inheritance;
void event(const char *name) {
    board::text(inheritance ? "EVENT mutex " : "EVENT binary ");
    board::text(name); board::text("\n");
}
void wait_start() { configASSERT(ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 1); }
void done() { xTaskNotifyGive(coordinator_handle); }
void low(void *) {
    for (;;) {
        wait_start();
        configASSERT(xSemaphoreTake(active_lock, portMAX_DELAY) == pdPASS);
        event("LOW_LOCK");
        // Coordinator (priority 4) preempts here, releases H and M, then blocks.
        // H (3) attempts the held lock before either L (1) or M (2) can run.
        xTaskNotifyGive(coordinator_handle);
        configASSERT(uxTaskPriorityGet(nullptr) == (inheritance ? 3U : 1U));
        event(inheritance ? "LOW_RUN priority=3" : "LOW_RUN priority=1");
        configASSERT(xSemaphoreGive(active_lock) == pdPASS);
        done();
    }
}
void medium(void *) {
    for (;;) {
        wait_start(); event("MEDIUM_RUN");
        // Bounded runnable work. Event order, not elapsed time, is the assertion.
        volatile uint32_t work = 1;
        for (unsigned i = 0; i < 5000; ++i) work = work * 1664525U + 1013904223U;
        done();
    }
}
void high(void *) {
    for (;;) {
        wait_start(); event("HIGH_WAIT");
        configASSERT(xSemaphoreTake(active_lock, portMAX_DELAY) == pdPASS);
        event("HIGH_LOCK");
        configASSERT(xSemaphoreGive(active_lock) == pdPASS);
        done();
    }
}
void print_words(const char *label, uint32_t value) {
    char digits[11]; unsigned at = 0;
    do { digits[at++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    board::text(label);
    while (at) { char one[2] = {digits[--at], 0}; board::text(one); }
    board::text("\n");
}
void coordinator(void *) {
    for (unsigned phase = 0; phase < 2; ++phase) {
        inheritance = phase != 0; active_lock = inheritance ? mutex : binary;
        xTaskNotifyGive(low_handle);
        // Consume exactly the 'lock held' handshake.
        configASSERT(ulTaskNotifyTake(pdFALSE, portMAX_DELAY) == 1);
        // Both become ready while C is still running, so H necessarily runs first.
        xTaskNotifyGive(high_handle); xTaskNotifyGive(medium_handle);
        for (unsigned done_count = 0; done_count < 3; ++done_count)
            (void)ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        event("DONE");
    }
    print_words("WATERMARK coordinator=", uxTaskGetStackHighWaterMark(nullptr));
    print_words("WATERMARK low=", uxTaskGetStackHighWaterMark(low_handle));
    print_words("WATERMARK medium=", uxTaskGetStackHighWaterMark(medium_handle));
    print_words("WATERMARK high=", uxTaskGetStackHighWaterMark(high_handle));
    board::text("EXPERIMENT_OK\n"); board::stop(0);
}
}
extern "C" void project_panic(const char *message) { board::text(message); board::stop(3); }
extern "C" void firmware_main() {
    board::uart_init();
    *reinterpret_cast<volatile uint32_t *>(0xe000ed0c) = 0x05fa0000;
    binary = xSemaphoreCreateBinaryStatic(&binary_tcb);
    mutex = xSemaphoreCreateMutexStatic(&mutex_tcb);
    configASSERT(binary && mutex && xSemaphoreGive(binary) == pdPASS);
    coordinator_handle = xTaskCreateStatic(coordinator, "coordinate", 384, nullptr, 4, coordinator_stack, &coordinator_tcb);
    low_handle = xTaskCreateStatic(low, "low", 256, nullptr, 1, low_stack, &low_tcb);
    medium_handle = xTaskCreateStatic(medium, "medium", 256, nullptr, 2, medium_stack, &medium_tcb);
    high_handle = xTaskCreateStatic(high, "high", 256, nullptr, 3, high_stack, &high_tcb);
    configASSERT(coordinator_handle && low_handle && medium_handle && high_handle);
    vTaskStartScheduler(); project_panic("SCHEDULER_RETURN\n");
}
