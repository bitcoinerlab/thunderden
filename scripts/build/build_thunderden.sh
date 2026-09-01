#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Build Thunder Den via Buildroot external tree.

Usage:
  build_thunderden.sh --buildroot-dir <path> [--output-dir <path>] [--clean-output] [--menuconfig]

Options:
  --buildroot-dir <path>  Buildroot source directory (required)
  --output-dir <path>     Build output directory (default: <repo>/out/buildroot)
  --clean-output          Recreate the Buildroot output before building
  --menuconfig            Open menuconfig after loading defconfig
  -h, --help              Show this help

Notes:
  - Run scripts/build/fetch_bitcoin_bash_tools.sh first.
  - Linux host only.
  - Build runs in 2 phases: configuration, then the full system image.
  - Resulting binaries are in <output-dir>/images.
EOF
}

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
VERSIONS_FILE="${ROOT_DIR}/scripts/build/versions.env"
[ -f "$VERSIONS_FILE" ] || { echo "Missing build pins: $VERSIONS_FILE" >&2; exit 1; }
. "$VERSIONS_FILE"

BR_EXTERNAL="${ROOT_DIR}/buildroot-external"
BUILDROOT_DIR=""
OUTPUT_DIR="${ROOT_DIR}/out/buildroot"
MENUCONFIG=0
CLEAN_OUTPUT=0
BBT_SRC="${THUNDERDEN_BBT_SRC:-${ROOT_DIR}/third_party/bitcoin-bash-tools}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 1
  }
}

calculate_build_config_hash() {
  (
    cd "$ROOT_DIR"
    {
      sha256sum \
        scripts/build/versions.env \
        scripts/build/Dockerfile \
        buildroot-external/Config.in \
        buildroot-external/external.desc \
        buildroot-external/external.mk \
        buildroot-external/board/thunderden/linux.config
      find \
        buildroot-external/configs \
        buildroot-external/package \
        buildroot-external/patches \
        -type f \( \
          -name 'Config.in' -o \
          -name '*.mk' -o \
          -name '*.hash' -o \
          -name '*.patch' \
        \) -print0 |
        LC_ALL=C sort -z |
        xargs -0 sha256sum
    } | sha256sum
  ) | {
    read -r hash _
    printf '%s\n' "$hash"
  }
}

clear_output_dir() {
  [ "$OUTPUT_DIR" != "/" ] || { echo "Refusing to clear /" >&2; exit 1; }
  find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
}

sanitize_path_for_buildroot() {
  local old_path="$PATH"
  local clean_path=""
  local entry=""
  local dropped=0

  while [ -n "$old_path" ]; do
    case "$old_path" in
      *:*) entry="${old_path%%:*}"; old_path="${old_path#*:}" ;;
      *) entry="$old_path"; old_path="" ;;
    esac

    case "$entry" in
      *[[:space:]]*)
        dropped=1
        ;;
      *)
        if [ -z "$clean_path" ]; then
          clean_path="$entry"
        else
          clean_path="${clean_path}:$entry"
        fi
        ;;
    esac
  done

  if [ "$dropped" -eq 1 ]; then
    echo "Sanitizing PATH for Buildroot (removed entries with spaces/tabs/newlines)." >&2
    PATH="$clean_path"
    export PATH
  fi
}

ensure_linux_host() {
  if [ "$(uname -s)" != "Linux" ]; then
    echo "This build helper supports Linux hosts only." >&2
    echo "Use a Linux VM or Linux machine for Thunder Den builds." >&2
    exit 1
  fi
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --buildroot-dir)
      [ "$#" -ge 2 ] || { echo "--buildroot-dir requires a value" >&2; exit 1; }
      BUILDROOT_DIR="$2"
      shift 2
      ;;
    --output-dir)
      [ "$#" -ge 2 ] || { echo "--output-dir requires a value" >&2; exit 1; }
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --clean-output)
      CLEAN_OUTPUT=1
      shift
      ;;
    --menuconfig)
      MENUCONFIG=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

[ -n "${BUILDROOT_DIR}" ] || {
  echo "--buildroot-dir is required" >&2
  usage
  exit 1
}

ensure_linux_host

need_cmd find
need_cmd sha256sum
need_cmd sort
need_cmd xargs

[ -f "${BUILDROOT_DIR}/Makefile" ] || {
  echo "Not a Buildroot source tree: ${BUILDROOT_DIR}" >&2
  exit 1
}

ACTUAL_BUILDROOT_VERSION="$(make -s -C "$BUILDROOT_DIR" print-version)"
if [ "$ACTUAL_BUILDROOT_VERSION" != "$BUILDROOT_VERSION" ]; then
  echo "Buildroot version mismatch: expected $BUILDROOT_VERSION, got $ACTUAL_BUILDROOT_VERSION" >&2
  exit 1
fi

[ -f "${BBT_SRC}/bitcoin.sh" ] || {
  echo "Missing bitcoin-bash-tools: ${BBT_SRC}" >&2
  echo "Run scripts/build/fetch_bitcoin_bash_tools.sh or set THUNDERDEN_BBT_SRC" >&2
  exit 1
}

mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

ACTUAL_BBT_SHA256="$(sha256sum "${BBT_SRC}/bitcoin.sh")"
ACTUAL_BBT_SHA256="${ACTUAL_BBT_SHA256%% *}"
if [ "$ACTUAL_BBT_SHA256" != "$BITCOIN_BASH_TOOLS_SHA256" ]; then
  echo "bitcoin-bash-tools hash mismatch: expected $BITCOIN_BASH_TOOLS_SHA256, got $ACTUAL_BBT_SHA256" >&2
  exit 1
fi

BITCOIN_HASH_FILE="${BR_EXTERNAL}/patches/bitcoin/${BITCOIN_VERSION}/bitcoin.hash"
LINUX_HASH_FILE="${BR_EXTERNAL}/patches/linux/${LINUX_VERSION}/linux.hash"
grep -Fq "${BITCOIN_SOURCE_SHA256}  bitcoin-${BITCOIN_VERSION}.tar.gz" "$BITCOIN_HASH_FILE" || {
  echo "Bitcoin source hash does not match versions.env: $BITCOIN_HASH_FILE" >&2
  exit 1
}
grep -Fq "${LINUX_SOURCE_SHA256}  linux-${LINUX_VERSION}.tar.xz" "$LINUX_HASH_FILE" || {
  echo "Linux source hash does not match versions.env: $LINUX_HASH_FILE" >&2
  exit 1
}
grep -Fqx "BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE=\"${LINUX_VERSION}\"" \
  "${BR_EXTERNAL}/configs/thunderden_x86_64_defconfig" || {
  echo "Linux version in defconfig does not match versions.env" >&2
  exit 1
}

BUILD_CONFIG_HASH="$(calculate_build_config_hash)"
BUILD_CONFIG_STAMP="${OUTPUT_DIR}/.thunderden-build-config-sha256"
PREVIOUS_BUILD_CONFIG_HASH=""
[ ! -f "$BUILD_CONFIG_STAMP" ] || PREVIOUS_BUILD_CONFIG_HASH="$(< "$BUILD_CONFIG_STAMP")"

if [ "$CLEAN_OUTPUT" -eq 1 ] || [ "$PREVIOUS_BUILD_CONFIG_HASH" != "$BUILD_CONFIG_HASH" ]; then
  if [ -n "$(find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
    echo "Build pins or configuration changed; clearing output while preserving downloads."
    clear_output_dir
  fi
fi
printf '%s\n' "$BUILD_CONFIG_HASH" > "$BUILD_CONFIG_STAMP"

sanitize_path_for_buildroot

echo
echo "Buildroot pin: ${BUILDROOT_VERSION}"
echo "Linux pin: ${LINUX_VERSION}"
echo "Bitcoin Core pin: ${BITCOIN_VERSION}"

# Phase 1: load Thunder Den defconfig into the Buildroot output directory.
echo
echo "== [1/2] Loading Thunder Den configuration =="
make -C "${BUILDROOT_DIR}" O="${OUTPUT_DIR}" BR2_EXTERNAL="${BR_EXTERNAL}" thunderden_x86_64_defconfig

if [ "${MENUCONFIG}" -eq 1 ]; then
  echo
  echo "== Opening menuconfig =="
  make -C "${BUILDROOT_DIR}" O="${OUTPUT_DIR}" BR2_EXTERNAL="${BR_EXTERNAL}" menuconfig
fi

# Buildroot caches local package extract/build stamps in O=. For local-source
# packages this can keep stale binaries across incremental runs. Force a
# lightweight refresh of thunderden-qrscan so scanner code changes are picked
# up without nuking the whole cache.
if [ -f "${OUTPUT_DIR}/.config" ] && grep -q '^BR2_PACKAGE_THUNDERDEN_QRSCAN=y' "${OUTPUT_DIR}/.config"; then
  echo
  echo "== Refreshing Thunder Den QR scanner =="
  make -C "${BUILDROOT_DIR}" O="${OUTPUT_DIR}" BR2_EXTERNAL="${BR_EXTERNAL}" thunderden-qrscan-dirclean
fi

# Phase 2: Buildroot creates the toolchain, target packages, and image files.
echo
echo "== [2/2] Building Thunder Den =="
THUNDERDEN_BBT_SRC="${BBT_SRC}" \
  make -C "${BUILDROOT_DIR}" O="${OUTPUT_DIR}" BR2_EXTERNAL="${BR_EXTERNAL}" BITCOIN_VERSION="${BITCOIN_VERSION}"

echo
echo "Build complete. Artifacts: ${OUTPUT_DIR}/images"
echo "To create the hybrid BIOS+UEFI image:"
echo "  ${ROOT_DIR}/buildroot-external/board/thunderden/make-image.sh --binaries-dir ${OUTPUT_DIR}/images --output thunderden.img"
echo "To create the minimum-size x86_64 UEFI image:"
echo "  ${ROOT_DIR}/buildroot-external/board/thunderden/make-image.sh --binaries-dir ${OUTPUT_DIR}/images --output thunderden-small.img --small"
