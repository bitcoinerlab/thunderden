#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  thunderden_entropy_guard.sh [--quiet]

Purpose:
  - Ensure entropy trust flags are present on kernel cmdline.
  - Block until kernel CSPRNG is initialized.

Notes:
  - This command waits indefinitely (no timeout).
  - Intended for seed/mnemonic generation flows.
EOF
}

quiet=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --quiet)
      quiet=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'Entropy guard: unknown option: %s\n' "$1" >&2
      exit 1
      ;;
  esac
done

say() {
  [ "$quiet" -eq 1 ] || printf '%s\n' "$*" >&2
}

die() {
  printf 'Entropy guard: %s\n' "$*" >&2
  exit 1
}

[ -r /proc/cmdline ] || die 'missing /proc/cmdline'
[ -r /dev/random ] || die 'missing /dev/random'

cmdline="$(cat /proc/cmdline)"

case " ${cmdline} " in
  *" random.trust_cpu=off "*) ;;
  *) die 'required kernel arg missing: random.trust_cpu=off' ;;
esac

case " ${cmdline} " in
  *" random.trust_bootloader=off "*) ;;
  *) die 'required kernel arg missing: random.trust_bootloader=off' ;;
esac

say 'Entropy guard: waiting for kernel CSPRNG initialization...'

# Reading from /dev/random blocks until the kernel RNG is initialized.
head -c 1 /dev/random >/dev/null

say 'Entropy guard: kernel CSPRNG initialized.'
