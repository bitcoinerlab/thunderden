#!/bin/bash
set -euo pipefail

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Render input data as terminal QR.

Usage:
  thunderden_show_qr.sh [data]

If data is omitted, one line is read from stdin.
EOF
  exit 0
fi

if [ "$#" -gt 0 ]; then
  DATA="$1"
else
  IFS= read -r DATA || true
fi

[ -n "${DATA:-}" ] || die "No data provided"

command -v qrencode >/dev/null 2>&1 || die "Missing command: qrencode"

if [ "${#DATA}" -gt 2500 ]; then
  printf 'Warning: payload is long (%s bytes). Single-frame QR may fail in some wallets.\n' "${#DATA}" >&2
fi

qrencode -t ANSIUTF8 -l M -m 1 "$DATA"
