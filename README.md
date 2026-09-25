# ARM telemetry firmware lab

A firmware lab comparing a bare-metal main loop and static FreeRTOS tasks on **QEMU MPS2 AN385 / Cortex-M3**. Both use the same bounded binary protocol, synthetic samples, UART driver and CMSDK timer. A separate image demonstrates priority inversion with a binary semaphore and priority inheritance with a FreeRTOS mutex.

## Run

Prerequisites: Linux (Ubuntu 24.04 or WSL2 recommended), GNU Make, Git, Python >=3.10, host G++ with ASan/UBSan, ARM bare-metal GCC/G++ and newlib headers, `qemu-system-arm` supporting `mps2-an385`, GNU `timeout`. No Python packages, CMake, networking stack or sensor model are needed.

```bash
# Optional: installs pinned packages into Ubuntu 24.04; uses sudo if not root.
./tools/install_ubuntu.sh

# Fetch the pinned upstream FreeRTOS kernel (requires GitHub access).
make bootstrap
make -j2 all
make verify
```

Build baseline: ARM GCC **13.2.1**, host G++ **13.3.0**, binutils **2.42**, QEMU **8.2.2**, FreeRTOS-Kernel **V11.1.0**. Exact kernel commit and package pins are in `dependencies.lock`. See [verification notes](docs/VERIFICATION.md) for test coverage and expected output.

To keep dependency downloads outside the repository:

```bash
export DEPS_DIR="$HOME/.cache/arm-telemetry-firmware-lab"
make bootstrap
make -j2 all
make verify
```

`DEPS_DIR` must be the parent of `FreeRTOS-Kernel-V11.1.0`. The build verifies the commit and refuses edits to the kernel sources it uses. `CROSS`, `HOST_CXX`, `QEMU`, and `ARM_INCLUDES` can override tool paths. A relocated compiler may need `ARM_INCLUDES='-isystem /path/to/newlib/headers'`; do not set global C include environment variables because they also affect native tests.

## Demos and host input

```bash
make demo-baremetal  # Automatically emits and verifies 30 ordered samples, then exits
make demo-freertos  # Same demo; also reports stack watermarks
make experiment    # Prints and asserts both coordinated scheduling traces

python3 tools/harness.py --image build/baremetal.elf --scenario faults
python3 tools/harness.py --image build/freertos.elf --scenario overload
```

The host tool launches QEMU and sends valid, fragmented, corrupt, oversized and semantically invalid frames. The `Guest`/`Frame` helpers in `tools/harness.py` and `tools/wire.py` can send custom frames. Normal firmware stops after 30 timer ticks (nominal 3 seconds of QEMU guest time), or upon a valid Stop command. Every harness wait has a deadline; Make adds an overall scenario timeout. QEMU exits through `SYS_EXIT_EXTENDED` semihosting. Semihosting must be enabled; this stop mechanism is an emulator/demo facility.

UART0 uses a QEMU stdio mux with signals disabled; the host adapter doubles Ctrl-A for the mux, so the actual UART bytes still match the documented protocol. Do not pipe unadapted binary frames into the mux.

`make test` runs the native framing/FIFO tests. `make integration` runs both firmware harnesses and the mutex experiment. `make check-images` checks unresolved symbols, selected runtime symbols/sections and vector/main-stack bounds. `make verify` also checks that a global-constructor probe fails to link. `make clean` removes build outputs.

## Observed results

- Both images booted and produced 30 ordered timer samples without reported loss.
- A 32-sample burst retained the first eight queued samples and counted 24 newest-sample drops; subsequent Ping/Stop commands succeeded.
- Both recovered after CRC, length, truncation, version and command errors; host assertions matched the status counters.
- Binary semaphore: `LOW_LOCK → HIGH_WAIT → MEDIUM_RUN → LOW_RUN → HIGH_LOCK`.
- Mutex: `LOW_LOCK → HIGH_WAIT → LOW_RUN (priority 3) → HIGH_LOCK → MEDIUM_RUN`.
- All measured FreeRTOS stack watermarks were positive. These are exercised-path observations, not worst-case stack guarantees.

## Scope and limits

Firmware C++ uses C++17 without exceptions, RTTI or global constructors; startup, runtime functions and upstream kernel integration use C/assembly. There is no heap allocation in the firmware, including task/queue creation. Queues, RX storage and parser buffers have explicit limits. TX is polled by a single writer; this small demo does not model a fully asynchronous UART or physical UART receive loss.

CRC detects accidental corruption; it provides no authentication. Input floods can cause explicit discard/recovery, and control output has a one-second queue-wait budget. QEMU's UART backpressure can hide real hardware overruns; observed queue behavior does not establish physical interrupt latency, energy efficiency or hard real-time deadlines. Timings use an emulated 25 MHz peripheral clock, not measurements from a board.

CI runs `make HOST_CXX=g++-13 verify` on Ubuntu 24.04 with pinned compiler and QEMU packages.

See [protocol](docs/PROTOCOL.md), [architecture/tradeoffs](docs/ARCHITECTURE.md), [verification](docs/VERIFICATION.md) and [source/license notices](NOTICE.md). Build outputs and downloaded dependencies are not tracked in the repository.
