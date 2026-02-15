#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Create a Thunder Den GPT UEFI disk image.

Usage:
  make-uefi-image.sh [--binaries-dir DIR] [--output FILE] [--size-mb N]

Defaults:
  --binaries-dir  current directory
  --output        thunderden-uefi.img
  --size-mb       512

Requirements:
  sudo privileges and Linux host tools: parted, losetup, mkfs.vfat,
  mount, umount, mountpoint.

Notes:
  This image boots a kernel with initramfs rootfs. It contains only an EFI
  system partition; no writable root partition is created.
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 1
  }
}

prepend_path_if_dir() {
  local dir="$1"

  [ -d "$dir" ] || return 0
  case ":$PATH:" in
    *":${dir}:"*) ;;
    *) PATH="${dir}:$PATH" ;;
  esac
}

ensure_host_path() {
  prepend_path_if_dir /usr/sbin
  prepend_path_if_dir /sbin
  prepend_path_if_dir /usr/local/sbin
  export PATH
}

BINARIES_DIR="$(pwd)"
OUTPUT_IMG="thunderden-uefi.img"
SIZE_MB="512"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --binaries-dir)
      [ "$#" -ge 2 ] || { echo "--binaries-dir requires a value" >&2; exit 1; }
      BINARIES_DIR="$2"
      shift 2
      ;;
    --output)
      [ "$#" -ge 2 ] || { echo "--output requires a value" >&2; exit 1; }
      OUTPUT_IMG="$2"
      shift 2
      ;;
    --size-mb)
      [ "$#" -ge 2 ] || { echo "--size-mb requires a value" >&2; exit 1; }
      SIZE_MB="$2"
      shift 2
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

EFI_DIR="${BINARIES_DIR}/efi-part"
KERNEL_IMG="${BINARIES_DIR}/bzImage"

ensure_host_path

[ -d "$EFI_DIR" ] || { echo "Missing directory: $EFI_DIR" >&2; exit 1; }
[ -f "$KERNEL_IMG" ] || { echo "Missing file: $KERNEL_IMG" >&2; exit 1; }

need_cmd sudo
need_cmd dd
need_cmd parted
need_cmd losetup
need_cmd mkfs.vfat
need_cmd mount
need_cmd umount
need_cmd mountpoint
need_cmd cp

case "$SIZE_MB" in
  ''|*[!0-9]*)
    echo "--size-mb must be a positive integer" >&2
    exit 1
    ;;
esac

dd if=/dev/zero of="$OUTPUT_IMG" bs=1M count="$SIZE_MB" status=progress

parted -s "$OUTPUT_IMG" \
  mklabel gpt \
  mkpart ESP fat32 1MiB 100% \
  set 1 esp on

LOOP_DEV=""
MNT_DIR="$(mktemp -d /tmp/thunderden-image.XXXXXX)"

cleanup() {
  set +e
  if mountpoint -q "${MNT_DIR}/efi"; then sudo umount "${MNT_DIR}/efi"; fi
  if [ -n "${LOOP_DEV}" ]; then sudo losetup -d "${LOOP_DEV}"; fi
  rm -rf "${MNT_DIR}"
}

trap cleanup EXIT INT TERM

LOOP_DEV="$(sudo losetup --find --show --partscan "$OUTPUT_IMG")"

sudo mkfs.vfat -F 32 -n THUNDEREFI "${LOOP_DEV}p1"

mkdir -p "${MNT_DIR}/efi"
sudo mount "${LOOP_DEV}p1" "${MNT_DIR}/efi"

sudo cp -r "${EFI_DIR}/." "${MNT_DIR}/efi/"
sudo cp -f "$KERNEL_IMG" "${MNT_DIR}/efi/bzImage"

sync

echo "Created UEFI image: $OUTPUT_IMG"
