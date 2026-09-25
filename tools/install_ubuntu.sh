#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Dependency installer for a clean Ubuntu 24.04 environment (or WSL2).
set -euo pipefail
cd "$(dirname "$0")/.."
source dependencies.lock
source /etc/os-release
[[ "$ID" == ubuntu && "$VERSION_ID" == 24.04 ]] || { echo 'Requires Ubuntu 24.04' >&2; exit 1; }
apt_source="$(mktemp)"
trap 'rm -f "$apt_source"' EXIT
chmod 644 "$apt_source"
cat > "$apt_source" <<SOURCES
deb https://snapshot.ubuntu.com/ubuntu/$UBUNTU_SNAPSHOT noble main universe
deb https://snapshot.ubuntu.com/ubuntu/$UBUNTU_SNAPSHOT noble-updates main universe
deb https://snapshot.ubuntu.com/ubuntu/$UBUNTU_SNAPSHOT noble-security main universe
SOURCES
if [[ "$(id -u)" == 0 ]]; then admin=(); else admin=(sudo); fi
apt_options=(-o "Dir::Etc::sourcelist=$apt_source" -o Dir::Etc::sourceparts=-)
"${admin[@]}" apt-get "${apt_options[@]}" update
"${admin[@]}" apt-get "${apt_options[@]}" install -y --no-install-recommends \
  "gcc-arm-none-eabi=$ARM_GCC_UBUNTU_PACKAGE" \
  "binutils-arm-none-eabi=$BINUTILS_UBUNTU_PACKAGE" \
  "libnewlib-dev=$NEWLIB_HEADERS_UBUNTU_PACKAGE" \
  "g++-13=$HOST_GXX_CI_PACKAGE" "qemu-system-arm=$QEMU_UBUNTU_PACKAGE" \
  g++ make git python3 ca-certificates
