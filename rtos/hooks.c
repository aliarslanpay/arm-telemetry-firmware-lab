/* SPDX-License-Identifier: MIT */
#include "FreeRTOS.h"
#include "task.h"
extern void project_panic(const char *message);
void project_assert_failed(const char *file, unsigned line) {
    (void)file; (void)line; project_panic("ASSERT\n");
}
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task; (void)name; project_panic("STACK_OVERFLOW\n");
}
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *depth) {
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *tcb = &idle_tcb; *stack = idle_stack; *depth = configMINIMAL_STACK_SIZE;
}
