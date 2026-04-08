#!/usr/bin/env bash
set -euo pipefail

PLATFORMS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
WORKSPACE_DIR="${WORKSPACE_DIR:-${PLATFORMS_DIR}/../edk2}"
PLATFORM_DSC="Platform/Mono/MonoGatewayPkg/MonoGatewayPkg.dsc"
BUILD_TYPE="${1:-DEBUG}"
TOOLCHAIN_TAG="${TOOLCHAIN_TAG:-GCC}"
BUILD_THREADS="${BUILD_THREADS:-1}"
YOCTO_TMP_DIR="${YOCTO_TMP_DIR:-${PLATFORMS_DIR}/../../meta-mono/build/tmp}"
BUILD_EXTRA_ARGS=("${@:2}")
RUN_BASETOOLS_TESTS="${RUN_BASETOOLS_TESTS:-0}"

if [[ ! -d "${WORKSPACE_DIR}" ]]; then
  echo "expected edk2 workspace at ${WORKSPACE_DIR}" >&2
  exit 1
fi

case "${BUILD_TYPE}" in
  DEBUG|RELEASE|NOOPT)
    ;;
  *)
    echo "usage: $0 [DEBUG|RELEASE|NOOPT]" >&2
    exit 1
    ;;
esac

if [[ "${TOOLCHAIN_TAG}" == "GCC" && -z "${GCC_AARCH64_PREFIX:-}" ]]; then
  GCC_CANDIDATE="$(
    {
      find "${YOCTO_TMP_DIR}" -path '*/work/cortexa72-oe-linux/atf/git/recipe-sysroot-native/usr/bin/aarch64-oe-linux/aarch64-oe-linux-gcc' -type f
      find "${YOCTO_TMP_DIR}" -path '*/recipe-sysroot-native/usr/bin/aarch64-oe-linux/aarch64-oe-linux-gcc' -type f
      find "${YOCTO_TMP_DIR}" -path '*gcc-cross-aarch64/usr/bin/aarch64-oe-linux/aarch64-oe-linux-gcc' -type f
    } | head -n 1 || true
  )"
  if [[ -n "${GCC_CANDIDATE}" ]]; then
    export GCC_AARCH64_PREFIX="${GCC_CANDIDATE%gcc}"
  else
    echo "set GCC_AARCH64_PREFIX, for example GCC_AARCH64_PREFIX=aarch64-linux-gnu-" >&2
    exit 1
  fi
fi

if [[ "${TOOLCHAIN_TAG}" == "GCC" && -n "${GCC_AARCH64_PREFIX:-}" ]]; then
  export PATH="${GCC_AARCH64_PREFIX%/*}:${PATH}"
fi

if [[ "${TOOLCHAIN_TAG}" == "GCC" ]] && ! command -v aarch64-oe-linux-as >/dev/null 2>&1; then
  BINUTILS_CANDIDATE="$(find "${YOCTO_TMP_DIR}" -path '*binutils-cross-aarch64*/usr/bin/aarch64-oe-linux/aarch64-oe-linux-as' -type f | head -n 1 || true)"
  if [[ -n "${BINUTILS_CANDIDATE}" ]]; then
    export PATH="${BINUTILS_CANDIDATE%/aarch64-oe-linux-as}:${PATH}"
  fi
fi

if ! command -v dtc >/dev/null 2>&1; then
  DTC_CANDIDATE="$(find "${YOCTO_TMP_DIR}" -path '*dtc-native/usr/bin/dtc' -type f | head -n 1 || true)"
  if [[ -n "${DTC_CANDIDATE}" ]]; then
    export PATH="${DTC_CANDIDATE%/dtc}:${PATH}"
  fi
fi

if ! command -v iasl >/dev/null 2>&1; then
  IASL_CANDIDATE="$(
    {
      find "${YOCTO_TMP_DIR}" -path '*acpica-native*/recipe-sysroot-native/usr/bin/iasl' -type f
      find "${YOCTO_TMP_DIR}" -path '*acpica-native*/image/usr/bin/iasl' -type f
      find "${YOCTO_TMP_DIR}" -path '*acpica-native*/packages-split/*/usr/bin/iasl' -type f
      find "${YOCTO_TMP_DIR}" -path '*recipe-sysroot-native/usr/bin/iasl' -type f
    } | head -n 1 || true
  )"
  if [[ -n "${IASL_CANDIDATE}" ]]; then
    export PATH="${IASL_CANDIDATE%/iasl}:${PATH}"
  else
    echo "iasl not found; install it on the host or build Yocto target acpica-native so AML-backed ACPI tables can be generated" >&2
    exit 1
  fi
fi

export WORKSPACE="${WORKSPACE_DIR}"
export PACKAGES_PATH="${WORKSPACE_DIR}:${PLATFORMS_DIR}"
export CCACHE_DISABLE=1
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"

make -C "${WORKSPACE_DIR}/BaseTools/Source/C"
make -C "${WORKSPACE_DIR}/BaseTools/Source/Python"
if [[ "${RUN_BASETOOLS_TESTS}" == "1" ]]; then
  make -C "${WORKSPACE_DIR}/BaseTools/Tests"
fi

pushd "${WORKSPACE_DIR}" >/dev/null
set +u
source "${WORKSPACE_DIR}/edksetup.sh" BaseTools
set -u
build \
  -a AARCH64 \
  -t "${TOOLCHAIN_TAG}" \
  -b "${BUILD_TYPE}" \
  -n "${BUILD_THREADS}" \
  -p "${PLATFORM_DSC}" \
  "${BUILD_EXTRA_ARGS[@]}"
popd >/dev/null
