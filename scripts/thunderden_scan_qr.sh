#!/bin/bash
set -euo pipefail

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

DEVICE="${1:-/dev/video0}"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Scan a single QR code and print base64 PSBT.

Usage:
  thunderden_scan_qr.sh [video_device]

Default device: /dev/video0
EOF
  exit 0
fi

if command -v zbarcam >/dev/null 2>&1; then
  PSBT="$(zbarcam --raw --quiet --oneshot "$DEVICE" 2>/dev/null | sed -n '/^cHNidP/p' | sed -n '1p')"
else
  printf 'zbarcam is not available. Paste unsigned PSBT (base64):\n' >&2
  IFS= read -r PSBT || true
fi

case "$PSBT" in
  cHNidP*)
    printf '%s\n' "$PSBT"
    ;;
  *)
    die "No valid base64 PSBT found"
    ;;
esac
