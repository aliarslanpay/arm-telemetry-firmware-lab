// SPDX-License-Identifier: MIT
#include <stdint.h>
static volatile uint32_t initialized = 0x13579bdf;
static volatile uint32_t zero_initialized;
static void put(const char *p) {
    auto *uart = reinterpret_cast<volatile uint32_t *>(0x40004000);
    uart[4] = 25000000 / 115200;
    uart[2] = 3;
    while (*p) { while (uart[1] & 1) {} uart[0] = static_cast<uint8_t>(*p++); }
}
static void stop(uint32_t code) {
    const uint32_t args[2] = {0x20026, code};
    register uint32_t r0 __asm("r0") = 0x20; // SYS_EXIT_EXTENDED
    register const uint32_t *r1 __asm("r1") = args;
    __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    for (;;) {}
}
extern "C" void platform_fault() { put("FAULT\n"); stop(2); }
extern "C" void firmware_main() {
    const bool ok = initialized == 0x13579bdf && zero_initialized == 0;
    put(ok ? "BOOT_OK data=1 bss=1\n" : "BOOT_FAIL\n");
    stop(ok ? 0 : 1);
}
