#!/bin/bash
set -euo pipefail

umask 077

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  printf '%s\n' "mnemonic words" | thunderden_export_bip84_descriptor.sh [options]

Options:
  --network <chain>       main | testnet | signet | regtest (default: testnet)
  --no-qr                 Print descriptor only, for headless tests
  -h, --help              Show this help

Environment:
  THUNDERDEN_BBT_SH       Path to bitcoin-bash-tools bitcoin.sh
  THUNDERDEN_SHOW_QR      Path to QR output script
  BIP39_PASSPHRASE        Optional BIP39 passphrase

Output:
  One BIP84 multipath descriptor using account xpub/tpub (never xprv):
  wpkh([fingerprint/84h/<coin>h/0h]xpub-or-tpub/<0;1>/*)
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Missing command: $1"
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

MNEMONIC=""
NETWORK="testnet"
NO_QR=0
BBT_SH="${THUNDERDEN_BBT_SH:-/opt/bitcoin-bash-tools/bitcoin.sh}"
SHOW_QR="${THUNDERDEN_SHOW_QR:-${SCRIPT_DIR}/thunderden_show_qr.sh}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --network)
      [ "$#" -ge 2 ] || die "--network requires a value"
      NETWORK="$2"
      shift 2
      ;;
    --no-qr)
      NO_QR=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown option: $1"
      ;;
  esac
done

IFS= read -r MNEMONIC || true
[ -n "$MNEMONIC" ] || die "Missing mnemonic on standard input"

case "$NETWORK" in
  main|testnet|signet|regtest) ;;
  *) die "Invalid --network value: $NETWORK" ;;
esac

need_cmd tr
need_cmd head
need_cmd tail
need_cmd mktemp
need_cmd basenc
need_cmd openssl
need_cmd dc

[ -f "$BBT_SH" ] || die "bitcoin-bash-tools script not found at: $BBT_SH"
if [ "$NO_QR" -eq 0 ]; then
  command -v "$SHOW_QR" >/dev/null 2>&1 || die "QR output script not found: $SHOW_QR"
fi

bbt_call() {
  local fn="$1"
  shift

  (
    set +u
    export LANG=C
    export LC_ALL=C
    BIP39_PASSPHRASE="${BIP39_PASSPHRASE-}"
    export BIP39_PASSPHRASE

    # shellcheck source=/dev/null
    . "$BBT_SH"

    case "$fn" in
      check-mnemonic)
        check-mnemonic "$@"
        ;;
      mnemonic-to-seed)
        mnemonic-to-seed "$@"
        ;;
      bip32)
        bip32 "$@" | base58 -c
        ;;
      master-fingerprint)
        [ "$#" -eq 1 ] || {
          printf 'master-fingerprint expects one xpub argument\n' >&2
          exit 2
        }
        base58 -d <<<"$1" |
          head -c 78 |
          tail -c 33 |
          hash160 |
          head -c 4 |
          basenc --base16 -w0
        ;;
      *)
        printf 'Unknown bitcoin-bash-tools function: %s\n' "$fn" >&2
        exit 2
        ;;
    esac
  )
}

IFS=' ' read -r -a MNEMONIC_WORDS <<<"$MNEMONIC"

case "${#MNEMONIC_WORDS[@]}" in
  12|15|18|21|24) ;;
  *) die "Mnemonic must contain 12, 15, 18, 21, or 24 words" ;;
esac

MNEMONIC_CHECK_RC=0
if bbt_call check-mnemonic "${MNEMONIC_WORDS[@]}"; then
  :
else
  MNEMONIC_CHECK_RC=$?
  case "$MNEMONIC_CHECK_RC" in
    1) die "Mnemonic contains unknown word(s). Use English BIP39 words only." ;;
    2) die "Mnemonic checksum is invalid. Use English BIP39 words only." ;;
    3) die "Mnemonic must contain 12, 15, 18, 21, or 24 English words." ;;
    *) die "Mnemonic validation failed (code: $MNEMONIC_CHECK_RC)" ;;
  esac
fi

WORKDIR="$(mktemp -d /tmp/thunderden-desc.XXXXXX)"
SEED_FILE="$WORKDIR/seed.bin"

cleanup() {
  rm -rf "$WORKDIR"
  unset MNEMONIC
  unset MNEMONIC_WORDS
}

trap cleanup EXIT INT TERM

bbt_call mnemonic-to-seed "${MNEMONIC_WORDS[@]}" > "$SEED_FILE" || die "Failed to derive BIP39 seed"
MNEMONIC=""
unset MNEMONIC_WORDS BIP39_PASSPHRASE

BIP32_ARGS=(-s)
ACCOUNT_PATH="/84h/0h/0h"
XPUB_PREFIX="xpub"
if [ "$NETWORK" != "main" ]; then
  BIP32_ARGS=(-t)
  ACCOUNT_PATH="/84h/1h/0h"
  XPUB_PREFIX="tpub"
fi

ACCOUNT_XPUB="$(bbt_call bip32 "${BIP32_ARGS[@]}" "${ACCOUNT_PATH}/N" < "$SEED_FILE" | tr -d '\r\n')"
[ -n "$ACCOUNT_XPUB" ] || die "Failed to derive BIP84 account xpub"
case "$ACCOUNT_XPUB" in
  ${XPUB_PREFIX}*) ;;
  *) die "Unexpected account key prefix for $NETWORK: ${ACCOUNT_XPUB:0:4}" ;;
esac

MASTER_XPUB="$(bbt_call bip32 "${BIP32_ARGS[@]}" "/N" < "$SEED_FILE" | tr -d '\r\n')"
[ -n "$MASTER_XPUB" ] || die "Failed to derive master xpub"

MASTER_FINGERPRINT_HEX="$(bbt_call master-fingerprint "$MASTER_XPUB" | tr -d '\r\n' | tr '[:upper:]' '[:lower:]')"
case "$MASTER_FINGERPRINT_HEX" in
  [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;;
  *) die "Failed to compute master fingerprint" ;;
esac

DESCRIPTOR="wpkh([${MASTER_FINGERPRINT_HEX}${ACCOUNT_PATH}]${ACCOUNT_XPUB}/<0;1>/*)"

printf 'Network: %s\n' "$NETWORK"
printf 'Account path: m%s\n' "$ACCOUNT_PATH"
printf 'Master fingerprint: %s\n' "$MASTER_FINGERPRINT_HEX"
printf 'Account xpub: %s\n\n' "$ACCOUNT_XPUB"
printf 'Descriptor:\n%s\n' "$DESCRIPTOR"

if [ "$NO_QR" -eq 0 ]; then
  printf '\nDescriptor QR:\n'
  "$SHOW_QR" "$DESCRIPTOR"
fi
