# Source origin and licenses

Project-owned code in `shared/`, `platform/`, `firmware/`, `rtos/`, `tests/`, and `tools/` is covered by the root MIT license. The FreeRTOS dependency and external tool licenses are described below.

FreeRTOS-Kernel V11.1.0 is an **unmodified upstream dependency**, at commit `dbf70559b27d39c1fdb68dfb9a32140b6a6777a0`. The application links upstream `tasks.c`, `queue.c`, `list.c`, and `portable/GCC/ARM_CM3/port.c`, using upstream headers. Kernel/port authorship is not attributed to this project. Upstream source retains its copyright/SPDX notices (including Amazon.com, Inc. or its affiliates). Its MIT license text is reproduced in [docs/FreeRTOS-LICENSE.md](docs/FreeRTOS-LICENSE.md). Distribute the upstream notices with firmware binaries that include the kernel. The repository does not include the kernel tree; `tools/bootstrap.sh` retrieves the pinned source.

QEMU and GCC/binutils/newlib are externally installed tools, not source embedded in the repository. QEMU's model and Arm's documentation were consulted for register and interrupt definitions; QEMU's GPL implementation was not copied into the project. These tools retain their own upstream licenses.
