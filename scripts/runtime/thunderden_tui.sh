#!/bin/bash
set -euo pipefail

umask 077

SIGNER="${THUNDERDEN_SIGNER:-/usr/bin/thunderden_sign_psbt.sh}"
SCANNER="${THUNDERDEN_SCANNER:-/usr/bin/thunderden-qrscan}"
SHOW_QR="${THUNDERDEN_SHOW_QR:-/usr/bin/thunderden_show_qr.sh}"
EXPORTER="${THUNDERDEN_EXPORTER:-/usr/bin/thunderden_export_bip84_descriptor.sh}"
NETWORK="${THUNDERDEN_NETWORK:-testnet}"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Thunder Den text interface.

Environment:
  THUNDERDEN_SIGNER   Path to signer script
  THUNDERDEN_SCANNER  Path to scanner script
  THUNDERDEN_SHOW_QR  Path to QR output script
  THUNDERDEN_EXPORTER Path to descriptor export script
  THUNDERDEN_NETWORK  main|testnet|signet|regtest
EOF
  exit 0
fi

cleanup_tty() {
  stty echo 2>/dev/null || true
}

trap cleanup_tty EXIT INT TERM

network_label() {
  case "$1" in
    main) printf 'Mainnet' ;;
    testnet) printf 'Testnet' ;;
    signet) printf 'Signet' ;;
    regtest) printf 'Regtest' ;;
    *) printf 'Unknown (%s)' "$1" ;;
  esac
}

set_network() {
  local choice=""

  while true; do
    clear
    printf '==========================================\n'
    printf '              NETWORK SELECTOR\n'
    printf '==========================================\n\n'
    printf 'Current network: %s\n\n' "$(network_label "$NETWORK")"
    printf '1) Mainnet\n'
    printf '2) Testnet\n'
    printf '3) Back\n\n'
    printf 'Choose an option: '

    IFS= read -r choice || true
    case "$choice" in
      1)
        NETWORK="main"
        return 0
        ;;
      2)
        NETWORK="testnet"
        return 0
        ;;
      3)
        return 0
        ;;
      *)
        printf 'Invalid option.\n' >&2
        press_enter
        ;;
    esac
  done
}

press_enter() {
  printf '\nPress Enter to continue... '
  IFS= read -r _ || true
}

prompt_secret() {
  local prompt="$1"
  local value=""

  printf '%s' "$prompt" >&2
  stty -echo
  if ! IFS= read -r value; then
    stty echo
    printf '\n' >&2
    return 130
  fi
  stty echo
  printf '\n' >&2
  printf '%s' "$value"
}

prompt_visible() {
  local prompt="$1"
  local value=""

  printf '%s' "$prompt" >&2
  if ! IFS= read -r value; then
    printf '\n' >&2
    return 130
  fi
  printf '%s' "$value"
}

sign_flow() {
  local psbt="$1"
  local mnemonic=""
  local passphrase=""
  local signed_psbt=""

  printf 'Mnemonic input is visible (English BIP39 words only). Press Ctrl+C to cancel and return to menu.\n' >&2

  if ! mnemonic="$(prompt_visible 'Enter mnemonic words: ')"; then
    printf 'Mnemonic entry cancelled.\n' >&2
    return 130
  fi

  [ -n "$mnemonic" ] || {
    printf 'Mnemonic is required.\n' >&2
    return 1
  }

  printf 'Press Ctrl+C to cancel passphrase entry and return to menu.\n' >&2
  if ! passphrase="$(prompt_secret 'Enter optional passphrase (empty for none): ')"; then
    printf 'Passphrase entry cancelled.\n' >&2
    return 130
  fi

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
  if ! "$SHOW_QR" "$signed_psbt"; then
    press_enter
  fi
}

descriptor_export_flow() {
  local mnemonic=""
  local passphrase=""

  printf 'Mnemonic input is visible (English BIP39 words only). Press Ctrl+C to cancel and return to menu.\n' >&2

  if ! mnemonic="$(prompt_visible 'Enter mnemonic words: ')"; then
    printf 'Mnemonic entry cancelled.\n' >&2
    return 130
  fi

  [ -n "$mnemonic" ] || {
    printf 'Mnemonic is required.\n' >&2
    return 1
  }

  printf 'Press Ctrl+C to cancel passphrase entry and return to menu.\n' >&2
  if ! passphrase="$(prompt_secret 'Enter optional passphrase (empty for none): ')"; then
    printf 'Passphrase entry cancelled.\n' >&2
    return 130
  fi

  clear
  if [ -n "$passphrase" ]; then
    if ! printf '%s\n' "$mnemonic" | BIP39_PASSPHRASE="$passphrase" "$EXPORTER" --network "$NETWORK" --mnemonic-stdin; then
      printf 'Descriptor export failed.\n' >&2
      return 1
    fi
  else
    if ! printf '%s\n' "$mnemonic" | "$EXPORTER" --network "$NETWORK" --mnemonic-stdin; then
      printf 'Descriptor export failed.\n' >&2
      return 1
    fi
  fi

  mnemonic=""
  passphrase=""
}

menu() {
  clear
  printf '==========================================\n'
  printf '               THUNDER DEN\n'
  printf '==========================================\n\n'
  printf 'Offline PSBT signer (BIP84, multi-format QR)\n'
  printf 'Current network: %s\n\n' "$(network_label "$NETWORK")"
  printf '1) Scan unsigned PSBT from camera\n'
  printf '2) Change network\n'
  printf '3) Export wallet descriptor (xpub + QR)\n'
  printf '4) Power off\n\n'
  printf 'Choose an option: '
}

main() {
  local choice=""
  local psbt=""
  local scan_rc=0
  local sign_rc=0
  local export_rc=0

  case "$NETWORK" in
    main|testnet|signet|regtest) ;;
    *)
      NETWORK="testnet"
      ;;
  esac

  local tool=""
  for tool in "$SIGNER" "$SCANNER" "$SHOW_QR" "$EXPORTER"; do
    command -v "$tool" >/dev/null 2>&1 || {
      printf 'Required program not found: %s\n' "$tool" >&2
      exit 1
    }
  done

  while true; do
    menu
    IFS= read -r choice || true
    case "$choice" in
      1)
        if psbt="$($SCANNER)"; then
          :
        else
          scan_rc=$?
          if [ "$scan_rc" -eq 130 ]; then
            continue
          fi
          printf 'QR scan failed.\n' >&2
          press_enter
          continue
        fi

        if sign_flow "$psbt"; then
          :
        else
          sign_rc=$?
          if [ "$sign_rc" -eq 130 ]; then
            continue
          fi
          press_enter
        fi
        ;;
      2)
        set_network
        ;;
      3)
        if descriptor_export_flow; then
          :
        else
          export_rc=$?
          if [ "$export_rc" -eq 130 ]; then
            continue
          fi
          press_enter
        fi
        ;;
      4)
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
