#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  thunderden_entropy_guard.sh [--quiet]

Purpose:
  - Check the kernel random-source settings.
  - Wait until the kernel random generator is ready.

This is future work for mnemonic generation. It is not installed in the image.
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
      printf 'Entropy check: unknown option: %s\n' "$1" >&2
      exit 1
      ;;
  esac
done

say() {
  [ "$quiet" -eq 1 ] || printf '%s\n' "$*" >&2
}

die() {
  printf 'Entropy check: %s\n' "$*" >&2
  exit 1
}

[ -r /proc/cmdline ] || die 'cannot read /proc/cmdline'
[ -r /dev/random ] || die 'cannot read /dev/random'

cmdline="$(cat /proc/cmdline)"

case " ${cmdline} " in
  *" random.trust_cpu=off "*) ;;
  *) die 'required kernel setting is missing: random.trust_cpu=off' ;;
esac

case " ${cmdline} " in
  *" random.trust_bootloader=off "*) ;;
  *) die 'required kernel setting is missing: random.trust_bootloader=off' ;;
esac

say 'Waiting for the kernel random generator...'

# This read waits until the kernel random generator is ready.
head -c 1 /dev/random >/dev/null

say 'The kernel random generator is ready.'
