#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
VERSIONS_FILE="${ROOT_DIR}/scripts/build/versions.env"
[ -f "$VERSIONS_FILE" ] || { echo "Missing build pins: $VERSIONS_FILE" >&2; exit 1; }
. "$VERSIONS_FILE"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Fetch bitcoin-bash-tools at the pinned commit into third_party/.

Usage:
  fetch_bitcoin_bash_tools.sh
EOF
  exit 0
fi

if [ "$#" -ne 0 ]; then
  echo "Unexpected arguments. Use --help for usage." >&2
  exit 1
fi

for cmd in curl install mktemp sha256sum; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "Missing command: $cmd" >&2
    exit 1
  }
done

DEST_DIR="${ROOT_DIR}/third_party/bitcoin-bash-tools"
DEST_FILE="${DEST_DIR}/bitcoin.sh"
SOURCE_URL="https://raw.githubusercontent.com/grondilu/bitcoin-bash-tools/${BITCOIN_BASH_TOOLS_COMMIT}/bitcoin.sh"
TMP_FILE="$(mktemp "${TMPDIR:-/tmp}/thunderden-bitcoin-sh.XXXXXX")"

cleanup() {
  rm -f "$TMP_FILE"
}

trap cleanup EXIT INT TERM

mkdir -p "${ROOT_DIR}/third_party"
mkdir -p "$DEST_DIR"

if [ -f "$DEST_FILE" ]; then
  ACTUAL_BITCOIN_SH_SHA256="$(sha256sum "$DEST_FILE")"
  ACTUAL_BITCOIN_SH_SHA256="${ACTUAL_BITCOIN_SH_SHA256%% *}"
  if [ "$ACTUAL_BITCOIN_SH_SHA256" = "$BITCOIN_BASH_TOOLS_SHA256" ]; then
    echo "bitcoin-bash-tools ready at: ${DEST_DIR}"
    echo "Pinned commit: ${BITCOIN_BASH_TOOLS_COMMIT}"
    echo "Pinned bitcoin.sh SHA256: ${ACTUAL_BITCOIN_SH_SHA256}"
    exit 0
  fi
fi

curl --fail --location --retry 3 --output "$TMP_FILE" "$SOURCE_URL"

ACTUAL_BITCOIN_SH_SHA256="$(sha256sum "$TMP_FILE")"
ACTUAL_BITCOIN_SH_SHA256="${ACTUAL_BITCOIN_SH_SHA256%% *}"
if [ "${ACTUAL_BITCOIN_SH_SHA256}" != "${BITCOIN_BASH_TOOLS_SHA256}" ]; then
  echo "Pinned bitcoin.sh SHA256 mismatch: expected ${BITCOIN_BASH_TOOLS_SHA256}, got ${ACTUAL_BITCOIN_SH_SHA256}" >&2
  exit 1
fi

if [ -d "$DEST_FILE" ]; then
  rmdir "$DEST_FILE" 2>/dev/null || {
    echo "Expected a file but found a non-empty directory: $DEST_FILE" >&2
    exit 1
  }
fi

install -m 0644 "$TMP_FILE" "$DEST_FILE"

echo "bitcoin-bash-tools ready at: ${DEST_DIR}"
echo "Pinned commit: ${BITCOIN_BASH_TOOLS_COMMIT}"
echo "Pinned bitcoin.sh SHA256: ${ACTUAL_BITCOIN_SH_SHA256}"
