#!/bin/bash
set -euo pipefail

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

status() {
  printf 'Scanner: %s\n' "$*" >&2
}

normalize_payload() {
  local payload="$1"

  payload="${payload//$'\r'/}"
  payload="${payload#QR-Code: }"
  payload="${payload#QR-Code:}"
  printf '%s' "$payload"
}

scan_psbt_qr() {
  local line=""
  local payload=""
  local preview=""
  local attempt=0

  status "Using camera device: $DEVICE"
  status 'Hold a single-frame base64 PSBT QR in front of the camera.'
  status 'Waiting for payload that starts with cHNidP... (Ctrl+C to cancel)'

  while true; do
    attempt=$((attempt + 1))
    status "Opening camera stream (attempt $attempt)..."

    while IFS= read -r line; do
      [ -n "$line" ] || continue
      payload="$(normalize_payload "$line")"
      [ -n "$payload" ] || continue

      case "$payload" in
        cHNidP*)
          status 'Detected base64 PSBT payload.'
          printf '%s\n' "$payload"
          return 0
          ;;
        *)
          if [ "${#payload}" -gt 36 ]; then
            preview="${payload:0:36}..."
          else
            preview="$payload"
          fi
          status "Decoded non-PSBT QR payload: $preview"
          ;;
      esac
    done < <(zbarcam --raw --quiet --nodisplay "$DEVICE")

    status 'Camera stream ended or failed to open. Retrying in 1 second...'
    sleep 1
  done

  return 0
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
[ -r "$DEVICE" ] || die "Camera device is not readable: $DEVICE"

scan_psbt_qr || die "No valid base64 PSBT found"
