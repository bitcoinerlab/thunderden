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
  thunderden_sign_psbt.sh --psbt <base64> (--mnemonic "words..." | --mnemonic-stdin) [options]

Options:
  --psbt <base64>         Unsigned PSBT in base64 (must start with cHNidP)
  --psbt-file <path>      Read unsigned PSBT from file
  --mnemonic <words>      BIP39 mnemonic words as a single string
  --mnemonic-stdin        Read one line of mnemonic words from stdin
  --network <chain>       main | testnet | signet | regtest (default: main)
  --range <n>             Descriptor range upper bound (default: 200)
  --json                  Print full JSON RPC result (default: PSBT only)
  -h, --help              Show this help

Environment:
  THUNDERDEN_BBT_SH       Path to bitcoin-bash-tools bitcoin.sh
  BIP39_PASSPHRASE        Optional BIP39 passphrase

Notes:
  - This script uses Bitcoin Core RPC descriptorprocesspsbt.
  - It runs bitcoind with an ephemeral datadir and network disabled.
  - BIP39 mnemonic parsing is forced to English wordlist only.
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Missing command: $1"
}

PSBT=""
MNEMONIC=""
MNEMONIC_FROM_STDIN=0
NETWORK="main"
RANGE="200"
PRINT_JSON=0
BBT_SH="${THUNDERDEN_BBT_SH:-/opt/bitcoin-bash-tools/bitcoin.sh}"
CHAIN="main"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --psbt)
      [ "$#" -ge 2 ] || die "--psbt requires a value"
      PSBT="$2"
      shift 2
      ;;
    --psbt-file)
      [ "$#" -ge 2 ] || die "--psbt-file requires a value"
      [ -f "$2" ] || die "PSBT file not found: $2"
      PSBT="$(tr -d '\r\n' < "$2")"
      shift 2
      ;;
    --mnemonic)
      [ "$#" -ge 2 ] || die "--mnemonic requires a value"
      MNEMONIC="$2"
      shift 2
      ;;
    --mnemonic-stdin)
      MNEMONIC_FROM_STDIN=1
      shift
      ;;
    --network)
      [ "$#" -ge 2 ] || die "--network requires a value"
      NETWORK="$2"
      shift 2
      ;;
    --range)
      [ "$#" -ge 2 ] || die "--range requires a value"
      RANGE="$2"
      shift 2
      ;;
    --json)
      PRINT_JSON=1
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

[ -n "$PSBT" ] || die "Missing PSBT. Use --psbt or --psbt-file"

if [ "$MNEMONIC_FROM_STDIN" -eq 1 ]; then
  IFS= read -r MNEMONIC || true
fi

[ -n "$MNEMONIC" ] || die "Missing mnemonic. Use --mnemonic or --mnemonic-stdin"

case "$PSBT" in
  cHNidP*) ;;
  *) die "PSBT must be base64 and start with cHNidP" ;;
esac

case "$NETWORK" in
  main|testnet|signet|regtest) ;;
  *) die "Invalid --network value: $NETWORK" ;;
esac

CHAIN="$NETWORK"
if [ "$NETWORK" = "testnet" ]; then
  CHAIN="test"
fi

case "$RANGE" in
  ''|*[!0-9]*) die "--range must be a positive integer" ;;
esac

need_cmd bitcoind
need_cmd bitcoin-cli
need_cmd ip
need_cmd tr
need_cmd sed
need_cmd grep
need_cmd tail
need_cmd mktemp
need_cmd dc
need_cmd basenc

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
      check-mnemonic) check-mnemonic "$@" ;;
      mnemonic-to-seed) mnemonic-to-seed "$@" ;;
      bip32) bip32 "$@" | base58 -c ;;
      *)
        printf 'Unknown bitcoin-bash-tools function: %s\n' "$fn" >&2
        exit 2
        ;;
    esac
  )
}

[ -f "$BBT_SH" ] || die "bitcoin-bash-tools script not found at: $BBT_SH"

print_bitcoind_diagnostics() {
  local log=""
  local found=0

  if [ -s "$BITCOIND_START_LOG" ]; then
    printf 'bitcoind startup output:\n' >&2
    sed -n '1,120p' "$BITCOIND_START_LOG" >&2
  fi

  for log in "$DATADIR"/debug.log "$DATADIR"/*/debug.log "$DATADIR"/*/*/debug.log; do
    [ -f "$log" ] || continue
    found=1
    printf 'debug.log tail (%s):\n' "$log" >&2
    tail -n 120 "$log" >&2 || true
  done

  if [ "$found" -eq 0 ]; then
    printf 'No debug.log found under %s\n' "$DATADIR" >&2
  fi
}

ensure_loopback_up() {
  ip link set lo up >/dev/null 2>&1 || die "Unable to bring loopback interface up"

  # Idempotent: succeeds first time, ignored if already configured.
  ip -4 addr add 127.0.0.1/8 dev lo >/dev/null 2>&1 || true

  ip -4 addr show dev lo | grep -q '127\.0\.0\.1/8' || die "Loopback IPv4 address 127.0.0.1/8 is missing"
}

IFS=' ' read -r -a MNEMONIC_WORDS <<<"$MNEMONIC"

case "${#MNEMONIC_WORDS[@]}" in
  12|15|18|21|24) ;;
  *) die "Mnemonic must contain 12, 15, 18, 21, or 24 words" ;;
esac

WORKDIR="$(mktemp -d /tmp/thunderden-sign.XXXXXX)"
DATADIR="$WORKDIR/datadir"
SEED_FILE="$WORKDIR/seed.bin"
CONF_FILE="$DATADIR/bitcoin.conf"

cleanup() {
  bitcoin-cli -datadir="$DATADIR" -conf="$CONF_FILE" -chain="$CHAIN" stop >/dev/null 2>&1 || true
  rm -rf "$WORKDIR"
  unset MNEMONIC
  unset MNEMONIC_WORDS
}

trap cleanup EXIT INT TERM

mkdir -p "$DATADIR"

ensure_loopback_up

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

bbt_call mnemonic-to-seed "${MNEMONIC_WORDS[@]}" > "$SEED_FILE" || die "Failed to derive BIP39 seed"

BIP32_ARGS=(-s)
ACCOUNT_PATH="/84h/0h/0h"
if [ "$NETWORK" != "main" ]; then
  BIP32_ARGS=(-t)
  ACCOUNT_PATH="/84h/1h/0h"
fi

ACCOUNT_XPRV="$(bbt_call bip32 "${BIP32_ARGS[@]}" "$ACCOUNT_PATH" < "$SEED_FILE" | tr -d '\r\n')"
[ -n "$ACCOUNT_XPRV" ] || die "Failed to derive BIP84 account key"

EXT_DESC="wpkh(${ACCOUNT_XPRV}/0/*)"
INT_DESC="wpkh(${ACCOUNT_XPRV}/1/*)"

DESCRIPTORS_JSON="$(printf '[{"desc":"%s","range":[0,%s]},{"desc":"%s","range":[0,%s]}]' "$EXT_DESC" "$RANGE" "$INT_DESC" "$RANGE")"

cat > "$CONF_FILE" <<EOF
server=1
daemon=1
listen=0
dnsseed=0
discover=0
upnp=0
natpmp=0
networkactive=0
EOF

BITCOIND_START_LOG="$WORKDIR/bitcoind-start.log"
if ! bitcoind -datadir="$DATADIR" -conf="$CONF_FILE" -chain="$CHAIN" -nosettings=1 -daemonwait -noconnect -maxconnections=0 -rpcbind=127.0.0.1 -rpcallowip=127.0.0.1 >"$BITCOIND_START_LOG" 2>&1; then
  print_bitcoind_diagnostics
  die "bitcoind failed to start"
fi

RESULT_JSON="$(bitcoin-cli -datadir="$DATADIR" -conf="$CONF_FILE" -chain="$CHAIN" -named descriptorprocesspsbt psbt="$PSBT" descriptors="$DESCRIPTORS_JSON" bip32derivs=true finalize=true)"

[ -n "$RESULT_JSON" ] || die "descriptorprocesspsbt returned empty output"

if [ "$PRINT_JSON" -eq 1 ]; then
  printf '%s\n' "$RESULT_JSON"
  exit 0
fi

RESULT_ONE_LINE="$(printf '%s' "$RESULT_JSON" | tr -d '\n')"
SIGNED_PSBT="$(printf '%s' "$RESULT_ONE_LINE" | sed -n 's/.*"psbt"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
COMPLETE_FLAG="$(printf '%s' "$RESULT_ONE_LINE" | sed -n 's/.*"complete"[[:space:]]*:[[:space:]]*\([^,}[:space:]]*\).*/\1/p')"

[ -n "$SIGNED_PSBT" ] || die "Unable to parse signed PSBT from RPC output"

if [ -n "$COMPLETE_FLAG" ]; then
  printf 'complete=%s\n' "$COMPLETE_FLAG" >&2
fi

printf '%s\n' "$SIGNED_PSBT"
