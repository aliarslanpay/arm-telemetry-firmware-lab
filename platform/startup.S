/* SPDX-License-Identifier: MIT
 * Minimal Cortex-M3 startup for this project.
 */
.syntax unified
.cpu cortex-m3
.thumb
.section .vectors,"a",%progbits
.global vector_table
vector_table:
.word _stack_top
.word Reset_Handler
.word Default_Handler /* NMI */
.word Default_Handler /* HardFault */
.word Default_Handler /* MemManage */
.word Default_Handler /* BusFault */
.word Default_Handler /* UsageFault */
.word 0, 0, 0, 0
.word SVC_Handler
.word Default_Handler /* DebugMon */
.word 0
.word PendSV_Handler
.word SysTick_Handler
.word UART0_Handler /* IRQ 0: receive */
.rept 7
.word Default_Handler
.endr
.word Timer0_Handler /* IRQ 8 */
.rept 23
.word Default_Handler
.endr
.section .text.Default_Handler,"ax",%progbits
.thumb_func
.global Default_Handler
Default_Handler:
  b platform_fault
.weak SVC_Handler, PendSV_Handler, SysTick_Handler, UART0_Handler, Timer0_Handler
.thumb_set SVC_Handler, Default_Handler
.thumb_set PendSV_Handler, Default_Handler
.thumb_set SysTick_Handler, Default_Handler
.thumb_set UART0_Handler, Default_Handler
.thumb_set Timer0_Handler, Default_Handler
