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
DSDT_DIR="${PLATFORMS_DIR}/Platform/Mono/MonoGatewayPkg/AcpiTablesInclude/Dsdt"
DSDT_MAIN="${DSDT_DIR}/Dsdt.asl"

find_first_match() {
  local search_root="$1"
  local path_pattern="$2"

  find "${search_root}" -path "${path_pattern}" -type f -print -quit 2>/dev/null || true
}

refresh_dsdt_timestamp() {
  local fragment

  [[ -f "${DSDT_MAIN}" ]] || return 0

  while IFS= read -r fragment; do
    [[ "${fragment}" == "${DSDT_MAIN}" ]] && continue
    if [[ "${fragment}" -nt "${DSDT_MAIN}" ]]; then
      touch "${DSDT_MAIN}"
      echo "Touched ${DSDT_MAIN} because ${fragment} is newer"
      return 0
    fi
  done < <(find "${DSDT_DIR}" -maxdepth 1 -type f -name '*.asl' | sort)
}

refresh_dsdt_disassembly() {
  local aml_path
  local dsl_prefix

  aml_path="${WORKSPACE_DIR}/Build/MonoGatewayPkg/${BUILD_TYPE}_${TOOLCHAIN_TAG}/AARCH64/Platform/Mono/MonoGatewayPkg/AcpiTablesInclude/PlatformAcpiDsdtLib/OUTPUT/Dsdt/Dsdt.aml"
  [[ -f "${aml_path}" ]] || return 0

  dsl_prefix="${aml_path%.aml}"
  iasl -d -p "${dsl_prefix}" "${aml_path}" >/dev/null
}

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
      find_first_match "${YOCTO_TMP_DIR}" '*/work/cortexa72-oe-linux/atf/git/recipe-sysroot-native/usr/bin/aarch64-oe-linux/aarch64-oe-linux-gcc'
      find_first_match "${YOCTO_TMP_DIR}" '*/recipe-sysroot-native/usr/bin/aarch64-oe-linux/aarch64-oe-linux-gcc'
      find_first_match "${YOCTO_TMP_DIR}" '*gcc-cross-aarch64/usr/bin/aarch64-oe-linux/aarch64-oe-linux-gcc'
    } | sed -n '1p'
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
  BINUTILS_CANDIDATE="$(find_first_match "${YOCTO_TMP_DIR}" '*binutils-cross-aarch64*/usr/bin/aarch64-oe-linux/aarch64-oe-linux-as')"
  if [[ -n "${BINUTILS_CANDIDATE}" ]]; then
    export PATH="${BINUTILS_CANDIDATE%/aarch64-oe-linux-as}:${PATH}"
  fi
fi

if ! command -v dtc >/dev/null 2>&1; then
  DTC_CANDIDATE="$(find_first_match "${YOCTO_TMP_DIR}" '*dtc-native/usr/bin/dtc')"
  if [[ -n "${DTC_CANDIDATE}" ]]; then
    export PATH="${DTC_CANDIDATE%/dtc}:${PATH}"
  fi
fi

if ! command -v iasl >/dev/null 2>&1; then
  IASL_CANDIDATE="$(
    {
      find_first_match "${YOCTO_TMP_DIR}" '*acpica-native*/recipe-sysroot-native/usr/bin/iasl'
      find_first_match "${YOCTO_TMP_DIR}" '*acpica-native*/image/usr/bin/iasl'
      find_first_match "${YOCTO_TMP_DIR}" '*acpica-native*/packages-split/*/usr/bin/iasl'
      find_first_match "${YOCTO_TMP_DIR}" '*recipe-sysroot-native/usr/bin/iasl'
    } | sed -n '1p'
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

refresh_dsdt_timestamp

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
refresh_dsdt_disassembly
popd >/dev/null
