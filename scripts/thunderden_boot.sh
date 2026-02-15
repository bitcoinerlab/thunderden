#!/bin/bash
set -euo pipefail

HARDEN="${THUNDERDEN_HARDENING:-/usr/bin/thunderden_hardening.sh}"
TUI="${THUNDERDEN_TUI:-/usr/bin/thunderden_tui.sh}"
GUARD="${THUNDERDEN_RUNTIME_GUARD:-/usr/bin/thunderden_runtime_guard.sh}"
GUARD_ERROR=""

run_guard() {
  local output=""

  [ -x "$GUARD" ] || {
    GUARD_ERROR="Runtime guard script not found: $GUARD"
    return 1
  }

  if output="$($GUARD 2>&1)"; then
    GUARD_ERROR=""
    return 0
  fi

  GUARD_ERROR="$output"
  return 1
}

hold_on_guard_failure() {
  local answer=""

  while true; do
    clear
    cat <<EOF
==========================================
        THUNDER DEN RUNTIME ERROR
==========================================

Runtime policy check failed.

$GUARD_ERROR

System stays on this screen until checks pass.

Type:
  reboot    reboot system
  poweroff  shut down system

Press Enter to retry checks.
EOF

    printf '> '
    IFS= read -r answer || true
    case "$answer" in
      reboot)
        reboot
        ;;
      poweroff)
        poweroff
        ;;
      *)
        if run_guard; then
          exec "$TUI"
        fi
        ;;
    esac
  done
}

if [ -x "$HARDEN" ]; then
  "$HARDEN" || true
fi

if run_guard; then
  exec "$TUI"
fi

hold_on_guard_failure
