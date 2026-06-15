#!/bin/bash
#
# Build script for ArmVirtGzvm
# Usage: ./make.sh [DEBUG|RELEASE|NOOPT]
#
# Auto-detects native AArch64 builds — on an aarch64 host with no
# cross-compiler installed, it uses the native GCC toolchain directly.
#

set -e

BUILD_TARGET="${1:-RELEASE}"

# Auto-detect toolchain prefix
if [ -n "$GCC5_AARCH64_PREFIX" ]; then
  # User explicitly set it — honour that
  TOOLCHAIN="$GCC5_AARCH64_PREFIX"
elif [ "$(uname -m)" = "aarch64" ] && ! command -v aarch64-linux-gnu-gcc &>/dev/null; then
  # Native aarch64 with no cross-compiler — use bare gcc
  TOOLCHAIN=""
else
  # Cross-compile (or aarch64 host that also has the cross-toolchain)
  TOOLCHAIN="aarch64-linux-gnu-"
fi

# Validate build target
case "$BUILD_TARGET" in
  DEBUG|RELEASE|NOOPT) ;;
  *) echo "Usage: $0 [DEBUG|RELEASE|NOOPT]" >&2; exit 1 ;;
esac

clear && \
unset WORKSPACE EDK_TOOLS_PATH CONF_PATH PACKAGES_PATH PYTHONPATH && \
git submodule update --init --recursive && \
make -C BaseTools clean && make -C BaseTools && \
mkdir -p Conf && \
. edksetup.sh BaseTools && \
GCC5_AARCH64_PREFIX="$TOOLCHAIN" build \
  -a AARCH64 \
  -t GCC5 \
  -b "$BUILD_TARGET" \
  -p ArmVirtPkg/ArmVirtGzvm.dsc

# Rename the FD file to the canonical lower-case form
FD="Build/ArmVirtGzvm-AArch64/${BUILD_TARGET}_GCC5/FV/EDK2-AARCH64-GZVM.fd"
if [ -f "$FD" ]; then
  mv -f "$FD" "$(dirname "$FD")/edk2-aarch64-gzvm.fd"
fi
