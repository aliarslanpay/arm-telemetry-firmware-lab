/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>
void project_assert_failed(const char *file, unsigned line);
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 0
#define configCPU_CLOCK_HZ 25000000UL
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE 128
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configUSE_TIMERS 0
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TRACE_FACILITY 0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configCHECK_HANDLER_INSTALLATION 1
/* Register-format priorities; PRIGROUP 0 maximizes preemption bits.
 * The CM3 port checks the 8-bit implementation case (threshold LSB must be 0).
 * UART0/Timer0 = 0x80, syscall threshold = 0x40, kernel = lowest urgency. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0x40
#define configKERNEL_INTERRUPT_PRIORITY 0xff
#define configASSERT(x) do { if (!(x)) project_assert_failed(__FILE__, __LINE__); } while (0)
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelay 0
#define INCLUDE_vTaskDelete 0
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle 1
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#endif
