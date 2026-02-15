#!/bin/bash
set -euo pipefail

umask 077

SIGNER="${THUNDERDEN_SIGNER:-/usr/bin/thunderden_sign_psbt.sh}"
SCANNER="${THUNDERDEN_SCANNER:-/usr/bin/thunderden_scan_qr.sh}"
SHOW_QR="${THUNDERDEN_SHOW_QR:-/usr/bin/thunderden_show_qr.sh}"
NETWORK="${THUNDERDEN_NETWORK:-main}"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Thunder Den text interface.

Environment:
  THUNDERDEN_SIGNER   Path to signer script
  THUNDERDEN_SCANNER  Path to scanner script
  THUNDERDEN_SHOW_QR  Path to QR output script
  THUNDERDEN_NETWORK  main|testnet|signet|regtest
EOF
  exit 0
fi

cleanup_tty() {
  stty echo 2>/dev/null || true
}

trap cleanup_tty EXIT INT TERM

press_enter() {
  printf '\nPress Enter to continue... '
  IFS= read -r _ || true
}

prompt_secret() {
  local prompt="$1"
  local value=""

  printf '%s' "$prompt" >&2
  stty -echo
  IFS= read -r value || true
  stty echo
  printf '\n' >&2
  printf '%s' "$value"
}

sign_flow() {
  local psbt="$1"
  local mnemonic=""
  local passphrase=""
  local signed_psbt=""

  mnemonic="$(prompt_secret 'Enter mnemonic words: ')"
  [ -n "$mnemonic" ] || {
    printf 'Mnemonic is required.\n' >&2
    return 1
  }

  passphrase="$(prompt_secret 'Enter optional passphrase (empty for none): ')"

  if [ -n "$passphrase" ]; then
    if ! signed_psbt="$(printf '%s\n' "$mnemonic" | BIP39_PASSPHRASE="$passphrase" "$SIGNER" --network "$NETWORK" --mnemonic-stdin --psbt "$psbt")"; then
      printf 'Signing failed.\n' >&2
      return 1
    fi
  else
    if ! signed_psbt="$(printf '%s\n' "$mnemonic" | "$SIGNER" --network "$NETWORK" --mnemonic-stdin --psbt "$psbt")"; then
      printf 'Signing failed.\n' >&2
      return 1
    fi
  fi

  mnemonic=""
  passphrase=""

  clear
  printf 'Signed PSBT:\n\n%s\n\n' "$signed_psbt"
  "$SHOW_QR" "$signed_psbt" || true
  press_enter
}

menu() {
  clear
  cat <<'EOF'
==========================================
               THUNDER DEN
==========================================

Offline PSBT signer (BIP84, single-frame QR)

1) Scan unsigned PSBT from camera
2) Power off

EOF
  printf 'Choose an option: '
}

main() {
  local choice=""
  local psbt=""

  command -v "$SIGNER" >/dev/null 2>&1 || {
    printf 'Signer script not found: %s\n' "$SIGNER" >&2
    exit 1
  }

  while true; do
    menu
    IFS= read -r choice || true
    case "$choice" in
      1)
        if ! psbt="$($SCANNER)"; then
          printf 'QR scan failed.\n' >&2
          press_enter
          continue
        fi
        if ! sign_flow "$psbt"; then
          press_enter
        fi
        ;;
      2)
        poweroff
        ;;
      *)
        printf 'Invalid option.\n' >&2
        press_enter
        ;;
    esac
  done
}

main "$@"
