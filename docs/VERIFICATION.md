# Verification

The checks run on Linux with QEMU MPS2 AN385 / Cortex-M3. They cover firmware behavior in emulation, not physical-board timing or electrical behavior.

## Prerequisites

GNU Make, Git, Python >=3.10, GNU `timeout`, host G++ with ASan/UBSan, ARM bare-metal GCC/G++ and newlib headers, and `qemu-system-arm` with `mps2-an385` support. Python tools use only the standard library.

Build baseline:

| Dependency | Version |
|---|---|
| ARM GCC/G++ | 13.2.1, Ubuntu `15:13.2.rel1-2` |
| ARM binutils | 2.42, Ubuntu `2.42-1ubuntu1+23` |
| newlib headers | Ubuntu `4.4.0.20231231-2`; no newlib binary/allocator linked |
| Native G++ | 13.3.0 |
| QEMU system ARM | 8.2.2, Ubuntu `1:8.2.2+ds-0ubuntu1.18` |
| FreeRTOS-Kernel | V11.1.0, `dbf70559b27d39c1fdb68dfb9a32140b6a6777a0`, GCC/ARM_CM3 port |
| GNU Make | 4.3 |

`dependencies.lock` and `tools/install_ubuntu.sh` select the Ubuntu package snapshot. The installer/CI uses host G++ package `13.3.0-6ubuntu2~24.04.1`; resource observations below also used the preceding `~24.04` revision, with the same compiler version. A relocated ARM compiler can use the `ARM_INCLUDES` override described in the README.

## Commands and coverage

```bash
# Optional package installation on Ubuntu 24.04.
./tools/install_ubuntu.sh
make bootstrap
make clean
make -j2 all
make HOST_CXX=g++-13 verify
```

`verify` runs the following checks. Each target can also be run separately:

| Command | Expected result / coverage |
|---|---|
| `make HOST_CXX=g++-13 test` | Native framing/FIFO tests pass under ASan/UBSan: CRC vector, every split point in an escaped maximum-payload frame, invalid length/version, corruption, short/dangling frames, body overrun, transport reset, deterministic arbitrary bytes, FIFO limits/order/wrap |
| `make boot-smoke` | `BOOT_OK data=1 bss=1`, QEMU exit 0 |
| `make integration` | Both telemetry images pass smoke, automatic, overload and fault scenarios; the separate inversion image passes its trace assertions |
| `make check-images` | No unresolved symbols or selected allocator/new/delete/exception/constructor symbols; no nonempty initializer/unwind sections from the checked list; `vector_table` at 0, stack bounds correct and `.bss` below the reserved main stack |
| `make check-constructors` | A probe containing a GCC global constructor fails to link with `Global constructors are unsupported`; the target fails if linking succeeds or fails for another reason |

The constructor check uses the firmware compiler/linker flags and boot image objects. `KEEP(*(.init_array*))` preserves the probe's entry despite `--gc-sections`, so the linker assertion rejects it. This checks the GCC `.init_array` path, not every possible C++ runtime initialization mechanism. Probe outputs stay in ignored `build/`.

### Telemetry scenarios

- **Smoke:** Ping/ACK round-trip and valid Stop, with no reported input errors.
- **Automatic:** 30 samples, sequences and ticks 1–30, synthetic values and timer source checked; status `(0,0,0,0,0,30,0,0)`. FreeRTOS also reports four positive stack watermarks within the allocated budgets.
- **Overload:** a 32-sample burst retains the first eight samples in FIFO order and counts 24 newest-sample drops; subsequent Ping/Stop succeeds.
- **Faults:** fragmented escaped sequence, corrupt CRC, 33-byte declared payload, truncated header, wrong version, invalid burst and unknown command. Recovery Ping follows each wire fault. The status prefix is `(8,1,1,4,0)`; output/timer drops are zero. Generated timer samples may be nonzero on a slower host.

The integration scenarios exercise output queue saturation. RX-ring, timer FIFO and RTOS input-event overflow policies are implemented but are not forced by these scenarios. Native tests exercise the shared bounded FIFO and parser, including transport reset.

### Demos and scheduling experiment

```bash
make demo-baremetal
make demo-freertos
make experiment
```

The demos run the automatic scenario. The experiment compares the complete event sequences and asserts L's effective priority:

```text
binary: LOW_LOCK, HIGH_WAIT, MEDIUM_RUN, LOW_RUN priority=1, HIGH_LOCK, DONE
mutex:  LOW_LOCK, HIGH_WAIT, LOW_RUN priority=3, HIGH_LOCK, MEDIUM_RUN, DONE
```

All workers complete and report positive stack watermarks. Event order is checked; no duration or speedup comparison is measured.

## Resource observations

```bash
arm-none-eabi-size build/boot.elf build/baremetal.elf build/freertos.elf build/inversion.elf
```

Representative section sizes with the baseline toolchain (bytes):

| Image | text | data | bss |
|---|---:|---:|---:|
| baremetal | 2488 | 0 | 520 |
| freertos telemetry | 8476 | 4 | 6916 |
| inversion experiment | 7364 | 4 | 6132 |
| boot smoke | 452 | 4 | 12 |

These exclude ELF debug data and the additional 4096-byte main/exception stack reservation. Static task stacks are included in `.bss`. Absolute dependency paths appear in assertion strings, so build paths can change `text` size.

Example unused stack words: telemetry acquisition/processing/output/idle `(221,423,320,118)`; experiment coordinator/low/medium/high `(348,222,220,224)`. One word is four bytes. Acquisition=219 and experiment high=222 have also been observed; asynchronous scheduler ticks can vary these path measurements. Watermarks are not worst-case stack bounds.

## Environment and test limits

- Restricted containers can block LeakSanitizer process inspection. If that occurs, use `ASAN_OPTIONS=detect_leaks=0 make HOST_CXX=g++-13 verify`; ASan/UBSan remain enabled. CI does not disable leak detection.
- QEMU runs under TCG without instruction-count determinism. Acquisition uses the emulated 25 MHz CMSDK clock at nominal 10 Hz; FreeRTOS uses a separate 1 kHz SysTick. Host waits use `time.monotonic()` and Make wraps scenarios with GNU `timeout`.
- Image checks inspect selected symbols/sections and address bounds. They do not prove absence of every runtime mechanism or worst-case stack safety.
- Control-queue timeout and stack-overflow panic hooks are not forced by the tests.
- Physical UART overruns/electrical behavior, interrupt latency, energy/power and hard-real-time deadlines require separate board tests. Network/cloud and safety validation are outside this lab.

CI is configured to install pinned packages, fetch the pinned kernel and run `make HOST_CXX=g++-13 verify` on Ubuntu 24.04.
