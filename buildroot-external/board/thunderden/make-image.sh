#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Create a Thunder Den disk image.

Usage:
  make-image.sh [--binaries-dir DIR] [--output FILE] [--small]

Defaults:
  --binaries-dir  current directory
  --output        thunderden.img (or thunderden-small.img with --small)

Requirements:
  Linux host tools: dd, parted, mkfs.vfat, mcopy, mdir, truncate, cp, wc.

Notes:
  Default mode creates a hybrid BIOS+UEFI image (FAT32, 64 MiB) for broad
  hardware compatibility.
  --small creates the smallest whole-MiB FAT16 image that fits the current
  payload. It boots x86_64 UEFI directly through the kernel EFI stub and does
  not contain GRUB or support legacy BIOS.
  Image assembly is rootless and does not use loop devices or mounts.
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
  prepend_path_if_dir "${BINARIES_DIR}/../host/sbin"
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
    return 1
  }

  IFS=':' read -r _ start _ size _ <<EOF
$line
EOF

  start="${start%B}"
  size="${size%B}"

  case "$start" in
    ''|*[!0-9]*)
      echo "Invalid partition start offset: $start" >&2
      return 1
      ;;
  esac

  case "$size" in
    ''|*[!0-9]*)
      echo "Invalid partition size: $size" >&2
      return 1
      ;;
  esac

  [ "$size" -gt 0 ] || {
    echo "Partition size must be > 0 bytes" >&2
    return 1
  }

  [ $((start % 512)) -eq 0 ] || {
    echo "Partition start is not 512-byte aligned: $start" >&2
    return 1
  }

  [ $((size % 512)) -eq 0 ] || {
    echo "Partition size is not 512-byte aligned: $size" >&2
    return 1
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
    echo "No files found to copy from: $source_dir" >&2
    return 1
  }

  for entry in "${entries[@]}"; do
    mcopy -i "$fat_image" -s "$entry" :: || return 1
  done
}

create_hybrid_compat_image() {
  local output_img="$1"
  local size_mb="64"
  local mnt_dir=""
  local esp_img=""
  local efi_staging_dir=""
  local part_meta=""
  local part_start_bytes=""
  local part_size_bytes=""
  local grub_core_size_bytes=""
  local grub_core_max_bytes=""

  mnt_dir="$(mktemp -d /tmp/thunderden-image.XXXXXX)" || return 1
  esp_img="${mnt_dir}/esp.vfat"
  efi_staging_dir="${mnt_dir}/efi-staging"

  (
    trap 'rm -rf "$mnt_dir"' EXIT

    dd if=/dev/zero of="$output_img" bs=1M count="$size_mb" status=none

    parted -s "$output_img" \
      mklabel msdos \
      mkpart primary fat32 1MiB 100% \
      set 1 boot on

    part_meta="$(extract_partition_start_and_size_bytes "$output_img")"
    part_start_bytes="${part_meta%%:*}"
    part_size_bytes="${part_meta##*:}"

    grub_core_size_bytes="$(wc -c < "$BIOS_GRUB_IMG")"
    grub_core_max_bytes="$((part_start_bytes - 512))"

    [ "$grub_core_max_bytes" -gt 0 ] || {
      echo "No post-MBR space available for BIOS GRUB core image" >&2
      exit 1
    }

    [ "$grub_core_size_bytes" -le "$grub_core_max_bytes" ] || {
      echo "grub.img (${grub_core_size_bytes} bytes) does not fit in post-MBR gap (${grub_core_max_bytes} bytes)" >&2
      exit 1
    }

    dd if="$BIOS_BOOT_IMG" of="$output_img" bs=440 count=1 conv=notrunc status=none
    dd if="$BIOS_GRUB_IMG" of="$output_img" bs=512 seek=1 conv=notrunc status=none

    truncate -s "$part_size_bytes" "$esp_img"
    mkfs.vfat -F 32 -n THUNDEREFI "$esp_img"

    mkdir -p "$efi_staging_dir"
    cp -r "$EFI_DIR/." "$efi_staging_dir/"
    cp -f "$KERNEL_IMG" "$efi_staging_dir/bzImage"
    mkdir -p "$efi_staging_dir/boot/grub"
    cp -f "$EFI_GRUB_CFG" "$efi_staging_dir/boot/grub/grub.cfg"

    copy_tree_into_fat_image "$esp_img" "$efi_staging_dir"

    dd if="$esp_img" of="$output_img" bs=512 seek="$((part_start_bytes / 512))" conv=notrunc status=none
  )
}

create_tiny_uefi_stub_image() {
  local output_img="$1"
  local size_mb="$2"
  local mnt_dir=""
  local esp_img=""
  local efi_staging_dir=""
  local part_meta=""
  local part_start_bytes=""
  local part_size_bytes=""

  mnt_dir="$(mktemp -d /tmp/thunderden-image.XXXXXX)" || return 1
  esp_img="${mnt_dir}/esp.vfat"
  efi_staging_dir="${mnt_dir}/efi-staging"

  (
    trap 'rm -rf "$mnt_dir"' EXIT

    dd if=/dev/zero of="$output_img" bs=1M count="$size_mb" status=none &&
      parted -s "$output_img" \
        mklabel msdos \
        mkpart primary fat16 1MiB 100% \
        set 1 boot on &&
      part_meta="$(extract_partition_start_and_size_bytes "$output_img")" &&
      part_start_bytes="${part_meta%%:*}" &&
      part_size_bytes="${part_meta##*:}" &&
      truncate -s "$part_size_bytes" "$esp_img" &&
      mkfs.vfat -F 16 -n THUNDEREFI "$esp_img" &&
      mkdir -p "$efi_staging_dir/EFI/BOOT" &&
      cp -f "$KERNEL_IMG" "$efi_staging_dir/EFI/BOOT/BOOTX64.EFI" &&
      copy_tree_into_fat_image "$esp_img" "$efi_staging_dir" &&
      dd if="$esp_img" of="$output_img" bs=512 seek="$((part_start_bytes / 512))" conv=notrunc status=none &&
      mdir -i "$output_img@@${part_start_bytes}" ::/EFI/BOOT/BOOTX64.EFI >/dev/null
  )
}

BINARIES_DIR="$(pwd)"
OUTPUT_IMG=""
SMALL_MODE=0

SMALL_PROBE_MIN_MB="8"
SMALL_PROBE_MAX_MB="64"

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
    --small)
      SMALL_MODE=1
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

if [ -z "$OUTPUT_IMG" ]; then
  if [ "$SMALL_MODE" -eq 1 ]; then
    OUTPUT_IMG="thunderden-small.img"
  else
    OUTPUT_IMG="thunderden.img"
  fi
fi

[ -d "$BINARIES_DIR" ] || { echo "Missing directory: $BINARIES_DIR" >&2; exit 1; }
BINARIES_DIR="$(cd "$BINARIES_DIR" && pwd)"

KERNEL_IMG="${BINARIES_DIR}/bzImage"
EFI_DIR="${BINARIES_DIR}/efi-part"
BIOS_BOOT_IMG="${BINARIES_DIR}/boot.img"
BIOS_GRUB_IMG="${BINARIES_DIR}/grub.img"
EFI_LOADER="${EFI_DIR}/EFI/BOOT/bootx64.efi"
EFI_GRUB_CFG="${EFI_DIR}/EFI/BOOT/grub.cfg"

ensure_host_path

[ -f "$KERNEL_IMG" ] || { echo "Missing file: $KERNEL_IMG" >&2; exit 1; }

if [ "$SMALL_MODE" -eq 0 ]; then
  [ -d "$EFI_DIR" ] || { echo "Missing directory: $EFI_DIR" >&2; exit 1; }
  [ -f "$BIOS_BOOT_IMG" ] || { echo "Missing file: $BIOS_BOOT_IMG" >&2; exit 1; }
  [ -f "$BIOS_GRUB_IMG" ] || { echo "Missing file: $BIOS_GRUB_IMG" >&2; exit 1; }
  [ -f "$EFI_LOADER" ] || { echo "Missing file: $EFI_LOADER" >&2; exit 1; }
  [ -f "$EFI_GRUB_CFG" ] || { echo "Missing file: $EFI_GRUB_CFG" >&2; exit 1; }
fi

need_cmd dd
need_cmd parted
need_cmd mkfs.vfat
need_cmd mcopy
need_cmd truncate
need_cmd cp
need_cmd wc

if [ "$SMALL_MODE" -eq 1 ]; then
  need_cmd mdir

  SMALL_SIZE_MB=""
  for ((size_mb = SMALL_PROBE_MIN_MB; size_mb <= SMALL_PROBE_MAX_MB; size_mb++)); do
    if create_tiny_uefi_stub_image "$OUTPUT_IMG" "$size_mb" >/dev/null 2>&1; then
      SMALL_SIZE_MB="$size_mb"
      break
    fi
  done

  [ -n "$SMALL_SIZE_MB" ] || {
    rm -f "$OUTPUT_IMG"
    echo "Failed to create small image: no size from ${SMALL_PROBE_MIN_MB}-${SMALL_PROBE_MAX_MB} MiB fits the current payload" >&2
    exit 1
  }

  echo "Created minimum-size UEFI-only image: $OUTPUT_IMG (FAT16, ${SMALL_SIZE_MB} MiB, no GRUB)"
  exit 0
fi

create_hybrid_compat_image "$OUTPUT_IMG"
echo "Created hybrid BIOS+UEFI image: $OUTPUT_IMG (FAT32, 64 MiB, max compatibility)"
