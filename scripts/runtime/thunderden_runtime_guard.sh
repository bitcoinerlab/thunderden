#!/bin/bash
set -euo pipefail

fail() {
  printf 'Runtime guard: %s\n' "$*" >&2
  return 1
}

mount_info() {
  local target="$1"
  local fstype=""
  local mount_point=""
  local options=""

  while IFS=' ' read -r _ mount_point fstype options _; do
    if [ "$mount_point" = "$target" ]; then
      printf '%s %s\n' "$fstype" "$options"
      return 0
    fi
  done < /proc/mounts

  return 1
}

has_mount_option() {
  local options="$1"
  local wanted="$2"

  case ",$options," in
    *",$wanted,"*) return 0 ;;
    *) return 1 ;;
  esac
}

check_root_ram_backed() {
  local fstype=""
  local options=""

  read -r fstype options < <(mount_info /) || fail "cannot read the root filesystem settings"
  case "$fstype" in
    rootfs|tmpfs|ramfs) ;;
    *)
      fail "the root filesystem is not in RAM (found: $fstype)"
      ;;
  esac

  has_mount_option "$options" ro || fail "the root filesystem is not read-only"
}

check_tmpfs_mount() {
  local target="$1"
  local fstype=""
  local options=""

  read -r fstype options < <(mount_info "$target") || fail "required mount is missing: $target"
  [ "$fstype" = "tmpfs" ] || fail "$target must be tmpfs (found: $fstype)"
  has_mount_option "$options" nosuid || fail "$target must use nosuid"
  has_mount_option "$options" nodev || fail "$target must use nodev"
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
