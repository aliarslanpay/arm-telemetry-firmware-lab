/* SPDX-License-Identifier: MIT */
#include <stdint.h>
extern uint32_t _data_start, _data_end, _data_load, _bss_start, _bss_end;
extern void firmware_main(void);
void Reset_Handler(void) {
    uint32_t *src = &_data_load;
    for (uint32_t *dst = &_data_start; dst < &_data_end; ++dst) *dst = *src++;
    for (uint32_t *dst = &_bss_start; dst < &_bss_end; ++dst) *dst = 0;
    firmware_main();
    for (;;) __asm volatile("wfi");
}
