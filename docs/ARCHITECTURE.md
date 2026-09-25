# Architecture and tradeoffs

The comparison deliberately holds framing, sample generation, UART register access, timer clock and limits constant. Bare-metal changes execution into a main loop; FreeRTOS changes execution into tasks and kernel queues. Neither target adds a sensor model or networking stack.

| Concern | Bare-metal | FreeRTOS |
|---|---|---|
| Receive interrupt | UART ISR, 128-byte ring | Same ISR/ring plus legal ISR notification |
| Acquisition clock | CMSDK Timer0 IRQ, bounded four-tick FIFO | Same Timer0 IRQ/FIFO; notify acquisition task |
| Framing and commands | Main loop, 128-byte work budget per pass | Processing task consumes bounded input events |
| Output | Eight-entry project FIFO; main-loop drain | Eight-entry static kernel queue; output task |
| Error state | Shared Parser/Application; platform loss counters | Same shared state plus discarded input-event counts |
| Scheduling evidence | Interrupt/main-loop synchronization | Priorities, notifications, static queues and separate mutex experiment |

## Reset, memory and peripherals

`platform/startup.S` owns the 48-entry vector table at address 0. Initial MSP is `0x20010000`; reset branches into C `.data` copy and `.bss` zeroing in `platform/reset.c`. `boot.cpp` actively checks a nonzero initialized variable and a zero-initialized variable. Startup does not call global constructors. The linker retains `.init_array*` inputs with `KEEP` and rejects a nonempty `.init_array`; `make check-constructors` tests this with a GCC global-constructor probe. This guard does not cover every possible C++ runtime initialization mechanism.

`platform/mps2.ld` reserves a 256 KiB low SSRAM partition for vectors/code and 64 KiB at `0x20000000` for writable data. Low CODE is a software partition of QEMU memory, not physical flash. The top 4 KiB of RAM is reserved for the main/exception stack and a linker assertion prevents `.bss` overlap. There is no MPU stack guard or bound on exception nesting established by this lab.

| Peripheral | Address/IRQ | Used registers |
|---|---|---|
| CMSDK UART0 | `0x40004000`, RX IRQ 0 | DATA +0, STATE +4, CTRL +8, INTSTATUS/CLEAR +12, BAUDDIV +16 |
| CMSDK Timer0 | `0x40000000`, IRQ 8 | CTRL +0, VALUE +4, RELOAD +8, INTSTATUS/CLEAR +12 |
| NVIC | ISER `0xe000e100`, IPR `0xe000e400` | Enable IRQ 0/8; priority bytes `0x80` |
| SCB | AIRCR `0xe000ed0c` | PRIGROUP 0 before FreeRTOS starts |

Both peripheral clocks are 25 MHz in the selected QEMU model. UART nominal baud is 115200 (integer divider 217). Timer reload is 2,500,000 at 10 Hz. Bare-metal does not install a SysTick handler; FreeRTOS uses the upstream CM3 SVC/PendSV/SysTick handlers with a 1 kHz scheduler tick. Exception handler routing and interrupt-priority validation remain enabled in the upstream port.

## Interrupt/task synchronization

`volatile` is used for **MMIO accesses**. It is not the synchronization policy for shared software state. Project rings/counters are ordinary storage; all thread/main access shared with interrupts is enclosed by saved/restored PRIMASK and compiler `memory` barriers. PRIMASK masks configurable exceptions and interrupts; NMI and HardFault remain outside that masking behavior. Both external ISRs have the same priority, so they do not preempt one another. These are Cortex-M execution assumptions, not a claim of ISO C++ thread portability or multicore safety.

Bare-metal checks for work and enters `WFI` while PRIMASK is set, then restores it. A pending interrupt wakes `WFI`, closing the check/sleep race. The RX ISR clears its interrupt **before** reading DATA: the QEMU receive callback may synchronously present the next byte when DATA is read. Leaving the new interrupt asserted avoids losing that byte. ISR work is limited to one byte, bounded ring bookkeeping and a task notification where applicable; no parsing, allocation or UART output occurs there.

FreeRTOS notifications use `vTaskNotifyGiveFromISR` with `portYIELD_FROM_ISR`. Numeric NVIC priority `0x80` has lower urgency than the nonzero syscall threshold `0x40`, allowing the ISR to call the kernel. PRIGROUP 0 maximizes preemption bits; the pinned port handles the eight-implemented-bit case, where one subpriority bit remains and the threshold LSB must be zero. Both `0x40` and `0x80` meet that condition. Kernel SysTick/PendSV run at lowest urgency; SVCall is configured by the upstream port. Task priorities and NVIC priority numbers are different scales.

The common Application/Parser are owned exclusively by processing/main context. In RTOS mode only the output task writes framed UART bytes. Acquisition emits the initial READY line before processing/output work begins. Higher-priority acquisition drains a finite byte ring/tick buffer, then blocks; processing can block for control-output space, allowing the lower-priority output task to drain. Explicitly suspending acquisition during finalization stabilizes input loss counters.

## Static resource budgets

| Telemetry task | Priority | Stack words / bytes |
|---|---|---|
| Acquisition | 3 | 256 / 1024 |
| Processing | 2 | 512 / 2048 |
| Output | 1 | 384 / 1536 |
| Idle | 0 | 128 / 512 |

Stacks total 5120 bytes plus static TCBs. Input queue: 64 × 8-byte events = 512 bytes plus its control block. Output queue: 8 × 44-byte items = 352 bytes plus its control block. Parser: 42-byte body, up to 86-byte encoded wire workspace per send. RX byte ring: 128 slots; timer FIFO: four slots. Runtime allocation is disabled (`configSUPPORT_DYNAMIC_ALLOCATION=0`); no FreeRTOS heap implementation or C++ allocator is linked. Main stack reservation is additional to task stacks and `.bss`.

The experiment uses coordinator priority 4 with 384 words, H priority 3, M priority 2 and L priority 1 with 256 words each, plus the 128-word idle stack. Kernel objects include one static binary semaphore and one static mutex. [Verification](VERIFICATION.md) lists actual image footprints and measured watermarks. Watermarks report minimum untouched words on executed paths; stack fill patterns and overflow hooks do not prove worst-case stack safety.

## Coordinated priority-inversion experiment

`firmware/inversion.cpp` is an independent image. At the start of each phase, all workers wait for notifications. Coordinator wakes L; L takes the selected lock and signals that it holds it. Priority-4 coordinator preempts L, makes H and M ready, then blocks. H necessarily runs first and blocks on L's lock.

With a binary semaphore there is no owner-based inheritance: runnable M (2) runs ahead of L (1), delaying H. With a mutex, H's wait raises L to priority 3; L runs ahead of M, releases the lock and allows H to acquire it. Coordinator waits for all three explicit completion signals before starting the next phase. There are no sleep-based timing guesses. Host assertions compare the entire phase event sequence; firmware also asserts L's observed effective priority.

Event order is the result. No scheduling speedup, physical wait-time metric, interrupt latency, energy or hard-real-time bound is inferred from this trace. A future board port would need memory/peripheral mapping, clock verification, physical UART tests, instrumentation and separate deadline/power evaluation.

## Primary references

- [QEMU MPS2 documentation](https://www.qemu.org/docs/master/system/arm/mps2.html)
- [Arm AN385, DAI0385D](https://documentation-service.arm.com/static/5ed107a5ca06a95ce53f89e3), memory map and IRQ assignments
- [Arm CMSDK APB UART programmer's model](https://support.arm.com/documentation/ddi0479/d/apb-components/apb-uart)
- [QEMU 8.2.2 MPS2 model](https://github.com/qemu/qemu/blob/v8.2.2/hw/arm/mps2.c), emulated clock and peripheral wiring
- [QEMU 8.2.2 CMSDK UART model](https://github.com/qemu/qemu/blob/v8.2.2/hw/char/cmsdk-apb-uart.c), W1C/status and receive behavior
- [Pinned official FreeRTOS CM3 port](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/dbf70559b27d39c1fdb68dfb9a32140b6a6777a0/portable/GCC/ARM_CM3/port.c), priority and vector validation
- [FreeRTOS Cortex-M interrupt priority guidance](https://www.freertos.org/RTOS-Cortex-M3-M4.html)
