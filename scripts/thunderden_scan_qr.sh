#!/bin/bash
set -euo pipefail

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
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

DEVICE="${1:-}"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Scan a single QR code and print base64 PSBT.

Usage:
  thunderden_scan_qr.sh [video_device]

Default device: auto-detect first /dev/video*
EOF
  exit 0
fi

command -v zbarcam >/dev/null 2>&1 || die "zbarcam is not available in this image"

if [ -z "$DEVICE" ]; then
  DEVICE="$(detect_default_device)" || die "No camera device found under /dev/video*"
fi

[ -c "$DEVICE" ] || die "Camera device is not available: $DEVICE"

PSBT="$(zbarcam --raw --quiet --oneshot "$DEVICE" 2>/dev/null | sed -n '/^cHNidP/p' | sed -n '1p')"

case "$PSBT" in
  cHNidP*)
    printf '%s\n' "$PSBT"
    ;;
  *)
    die "No valid base64 PSBT found"
    ;;
esac
