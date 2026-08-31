#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Build Thunder Den via Buildroot external tree.

Usage:
  build_thunderden.sh --buildroot-dir <path> [--output-dir <path>] [--menuconfig]

Options:
  --buildroot-dir <path>  Buildroot source directory (required)
  --output-dir <path>     Build output directory (default: <repo>/out/buildroot)
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
BR_EXTERNAL="${ROOT_DIR}/buildroot-external"
BUILDROOT_DIR=""
OUTPUT_DIR="${ROOT_DIR}/out/buildroot"
MENUCONFIG=0
BBT_SRC="${THUNDERDEN_BBT_SRC:-${ROOT_DIR}/third_party/bitcoin-bash-tools}"
THUNDERDEN_BITCOIN_VERSION="30.2"

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

[ -f "${BUILDROOT_DIR}/Makefile" ] || {
  echo "Not a Buildroot source tree: ${BUILDROOT_DIR}" >&2
  exit 1
}

[ -f "${BBT_SRC}/bitcoin.sh" ] || {
  echo "Missing bitcoin-bash-tools: ${BBT_SRC}" >&2
  echo "Run scripts/build/fetch_bitcoin_bash_tools.sh or set THUNDERDEN_BBT_SRC" >&2
  exit 1
}

mkdir -p "${OUTPUT_DIR}"

sanitize_path_for_buildroot

echo
echo "Bitcoin Core pin: ${THUNDERDEN_BITCOIN_VERSION}"

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
  make -C "${BUILDROOT_DIR}" O="${OUTPUT_DIR}" BR2_EXTERNAL="${BR_EXTERNAL}" BITCOIN_VERSION="${THUNDERDEN_BITCOIN_VERSION}"

echo
echo "Build complete. Artifacts: ${OUTPUT_DIR}/images"
echo "To create the hybrid BIOS+UEFI image:"
echo "  ${ROOT_DIR}/buildroot-external/board/thunderden/make-image.sh --binaries-dir ${OUTPUT_DIR}/images --output thunderden.img"
echo "To create the minimum-size x86_64 UEFI image:"
echo "  ${ROOT_DIR}/buildroot-external/board/thunderden/make-image.sh --binaries-dir ${OUTPUT_DIR}/images --output thunderden-small.img --small"
