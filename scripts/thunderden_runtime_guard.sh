#!/bin/bash
set -euo pipefail

fail() {
  printf 'Runtime guard: %s\n' "$*" >&2
  return 1
}

mount_fstype() {
  local target="$1"
  local fstype=""
  local mount_point=""

  while IFS=' ' read -r _ mount_point fstype _; do
    if [ "$mount_point" = "$target" ]; then
      printf '%s\n' "$fstype"
      return 0
    fi
  done < /proc/mounts

  return 1
}

check_root_ram_backed() {
  local fstype=""

  fstype="$(mount_fstype /)" || fail "cannot determine root filesystem type"
  case "$fstype" in
    rootfs|tmpfs|ramfs|overlay)
      return 0
      ;;
    *)
      fail "root filesystem is not RAM-backed (found: $fstype)"
      ;;
  esac
}

check_tmpfs_mount() {
  local target="$1"
  local fstype=""

  fstype="$(mount_fstype "$target")" || fail "required mount is missing: $target"
  [ "$fstype" = "tmpfs" ] || fail "$target must be tmpfs (found: $fstype)"
}

check_swap_off() {
  local lines=0

  while IFS= read -r _; do
    lines=$((lines + 1))
  done < /proc/swaps

  [ "$lines" -le 1 ] || fail "swap is enabled"
}

main() {
  check_root_ram_backed
  check_tmpfs_mount /tmp
  check_tmpfs_mount /run
  check_swap_off
}

main "$@"
