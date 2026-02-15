#!/bin/bash
set -euo pipefail

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

scan_psbt_qr() {
  local line=""

  printf 'Scanning on %s. Hold QR in front of camera. Press Ctrl+C to cancel.\n' "$DEVICE" >&2

  while IFS= read -r line; do
    [ -n "$line" ] || continue
    case "$line" in
      cHNidP*)
        printf '%s\n' "$line"
        return 0
        ;;
      *)
        printf 'Ignoring non-PSBT QR payload. Expected base64 PSBT (cHNidP...).\n' >&2
        ;;
    esac
  done < <(zbarcam --raw --quiet "$DEVICE" 2>/dev/null)

  return 1
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
Scan camera continuously until a base64 PSBT QR is found.

Usage:
  thunderden_scan_qr.sh [video_device]

Default device: auto-detect first /dev/video*
EOF
  exit 0
fi

trap 'die "Scan cancelled"' INT TERM

command -v zbarcam >/dev/null 2>&1 || die "zbarcam is not available in this image"

if [ -z "$DEVICE" ]; then
  DEVICE="$(detect_default_device)" || die "No camera device found under /dev/video*"
fi

[ -c "$DEVICE" ] || die "Camera device is not available: $DEVICE"

scan_psbt_qr || die "No valid base64 PSBT found"
