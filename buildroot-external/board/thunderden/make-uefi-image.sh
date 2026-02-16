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
  --size-mb       24

Requirements:
  Linux host tools: dd, parted, mkfs.vfat, mcopy, truncate.

Notes:
  This image boots a kernel with initramfs rootfs. It contains only an EFI
  system partition; no writable root partition is created.
  Image assembly is rootless and does not use loop devices or mounts.
  The helper uses FAT16 for very small images and FAT32 for larger images.
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
  prepend_path_if_dir "${BINARIES_DIR}/../host/bin"
  prepend_path_if_dir /usr/sbin
  prepend_path_if_dir /sbin
  prepend_path_if_dir /usr/local/sbin
  export PATH
}

extract_partition_start_and_size_bytes() {
  local image_path="$1"
  local line=""
  local start=""
  local size=""

  line="$({
    while IFS= read -r current; do
      case "$current" in
        1:*)
          printf '%s\n' "$current"
          break
          ;;
      esac
    done
  } < <(parted -ms "$image_path" unit B print))"

  [ -n "$line" ] || {
    echo "Unable to locate partition metadata in: $image_path" >&2
    exit 1
  }

  IFS=':' read -r _ start _ size _ <<EOF
$line
EOF

  start="${start%B}"
  size="${size%B}"

  case "$start" in
    ''|*[!0-9]*)
      echo "Invalid partition start offset: $start" >&2
      exit 1
      ;;
  esac

  case "$size" in
    ''|*[!0-9]*)
      echo "Invalid partition size: $size" >&2
      exit 1
      ;;
  esac

  [ "$size" -gt 0 ] || {
    echo "Partition size must be > 0 bytes" >&2
    exit 1
  }

  [ $((start % 512)) -eq 0 ] || {
    echo "Partition start is not 512-byte aligned: $start" >&2
    exit 1
  }

  [ $((size % 512)) -eq 0 ] || {
    echo "Partition size is not 512-byte aligned: $size" >&2
    exit 1
  }

  printf '%s:%s\n' "$start" "$size"
}

copy_tree_into_fat_image() {
  local fat_image="$1"
  local source_dir="$2"
  local entries=()

  shopt -s nullglob
  entries=("$source_dir"/*)
  shopt -u nullglob

  [ "${#entries[@]}" -gt 0 ] || {
    echo "No EFI files found to copy from: $source_dir" >&2
    exit 1
  }

  for entry in "${entries[@]}"; do
    mcopy -i "$fat_image" -s "$entry" ::
  done
}

BINARIES_DIR="$(pwd)"
OUTPUT_IMG="thunderden-uefi.img"
SIZE_MB="24"
FAT32_MIN_SIZE_MB="34"

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

[ -d "$BINARIES_DIR" ] || { echo "Missing directory: $BINARIES_DIR" >&2; exit 1; }
BINARIES_DIR="$(cd "$BINARIES_DIR" && pwd)"

EFI_DIR="${BINARIES_DIR}/efi-part"
KERNEL_IMG="${BINARIES_DIR}/bzImage"

ensure_host_path

[ -d "$EFI_DIR" ] || { echo "Missing directory: $EFI_DIR" >&2; exit 1; }
[ -f "$KERNEL_IMG" ] || { echo "Missing file: $KERNEL_IMG" >&2; exit 1; }

need_cmd dd
need_cmd parted
need_cmd mkfs.vfat
need_cmd mcopy
need_cmd truncate
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

MNT_DIR="$(mktemp -d /tmp/thunderden-image.XXXXXX)"
ESP_IMG="${MNT_DIR}/esp.vfat"
EFI_STAGING_DIR="${MNT_DIR}/efi-staging"
PART_START_BYTES=""
PART_SIZE_BYTES=""
FAT_TYPE="32"

cleanup() {
  rm -rf "${MNT_DIR}"
}

trap cleanup EXIT INT TERM

PART_META="$(extract_partition_start_and_size_bytes "$OUTPUT_IMG")"
PART_START_BYTES="${PART_META%%:*}"
PART_SIZE_BYTES="${PART_META##*:}"

# Very small ESP volumes are invalid/problematic as FAT32 on some firmware,
# so use FAT16 below this image-size threshold.
if [ "$SIZE_MB" -lt "$FAT32_MIN_SIZE_MB" ]; then
  FAT_TYPE="16"
fi

truncate -s "$PART_SIZE_BYTES" "$ESP_IMG"
mkfs.vfat -F "$FAT_TYPE" -n THUNDEREFI "$ESP_IMG"

mkdir -p "$EFI_STAGING_DIR"
cp -r "$EFI_DIR/." "$EFI_STAGING_DIR/"
cp -f "$KERNEL_IMG" "$EFI_STAGING_DIR/bzImage"

copy_tree_into_fat_image "$ESP_IMG" "$EFI_STAGING_DIR"

dd if="$ESP_IMG" of="$OUTPUT_IMG" bs=512 seek="$((PART_START_BYTES / 512))" conv=notrunc status=none

sync

echo "Created UEFI image: $OUTPUT_IMG"
