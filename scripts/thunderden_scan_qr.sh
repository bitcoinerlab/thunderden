#!/bin/bash
set -euo pipefail

status() {
  printf 'Scanner: %s\n' "$*" >&2
}

die() {
  status "$*"
  exit 1
}

cancelled() {
  status 'Scan cancelled by user.'
  exit 130
}

detect_default_device() {
  local dev=""

  for dev in /dev/video*; do
    [ -e "$dev" ] || continue
    [ -c "$dev" ] || continue
    printf '%s\n' "$dev"
    return 0
  done

  return 1
}

show_intro() {
  clear >/dev/tty 2>/dev/null || true

  cat >/dev/tty <<'EOF'
==========================================
               QR Scan Mode
==========================================

This will try to capture an unsigned PSBT QR code.
On successful capture, you will be prompted for the mnemonic.

If scanning is difficult, press any key to cancel and return.

Press Enter to start scanning...
EOF

  IFS= read -r _ < /dev/tty || return 1
  return 0
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Scan camera until a supported PSBT QR payload is found.

Usage:
  thunderden_scan_qr.sh [video_device]

Default device: auto-detect first /dev/video*
EOF
  exit 0
fi

trap 'cancelled' INT TERM

command -v thunderden-qrscan >/dev/null 2>&1 || die "thunderden-qrscan is not available in this image"

DEVICE="${1:-}"

if [ -z "$DEVICE" ]; then
  DEVICE="$(detect_default_device)" || die "No camera device found under /dev/video*"
fi

[ -c "$DEVICE" ] || die "Camera device is not available: $DEVICE"
[ -r "$DEVICE" ] || die "Camera device is not readable: $DEVICE"

show_intro || cancelled

exec thunderden-qrscan --device "$DEVICE"
