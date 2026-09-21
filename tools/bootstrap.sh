#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source dependencies.lock
# External cache can be supplied; default is ignored by Git.
dep_dir="${DEPS_DIR:-$PWD/.deps}"
kernel="$dep_dir/FreeRTOS-Kernel-$FREERTOS_TAG"
mkdir -p "$dep_dir"
if [[ ! -d "$kernel/.git" ]]; then
  git clone --depth 1 --branch "$FREERTOS_TAG" https://github.com/FreeRTOS/FreeRTOS-Kernel.git "$kernel"
fi
[[ "$(git -C "$kernel" rev-parse HEAD)" == "$FREERTOS_COMMIT" ]] || { echo 'Unexpected FreeRTOS commit' >&2; exit 1; }
command -v arm-none-eabi-gcc
command -v arm-none-eabi-g++
command -v qemu-system-arm
arm-none-eabi-gcc --version | head -n 1
qemu-system-arm --version | head -n 1
printf 'FreeRTOS directory: %s\n' "$kernel"
