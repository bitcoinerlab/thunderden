#!/bin/bash
set -euo pipefail

HARDEN="${THUNDERDEN_HARDENING:-/usr/bin/thunderden_hardening.sh}"
TUI="${THUNDERDEN_TUI:-/usr/bin/thunderden_tui.sh}"

if [ -x "$HARDEN" ]; then
  "$HARDEN" || true
fi

exec "$TUI"
