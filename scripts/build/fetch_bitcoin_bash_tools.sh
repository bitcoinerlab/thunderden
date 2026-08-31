#!/bin/bash
set -euo pipefail

PINNED_COMMIT="7fa496aa2004c55f7845a6cbd003007fb40694fc"
PINNED_BITCOIN_SH_SHA256="772d8d38f0cc215000815176deb555733fa22ff59e3154e280d6fb228af9397e"
REPO_URL="https://github.com/grondilu/bitcoin-bash-tools.git"

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

command -v sha256sum >/dev/null 2>&1 || {
  echo "Missing command: sha256sum" >&2
  exit 1
}

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
DEST_DIR="${ROOT_DIR}/third_party/bitcoin-bash-tools"

mkdir -p "${ROOT_DIR}/third_party"

if [ -d "${DEST_DIR}/.git" ]; then
  git -C "${DEST_DIR}" fetch origin "${PINNED_COMMIT}" --depth 1
else
  git clone "${REPO_URL}" "${DEST_DIR}"
fi

git -C "${DEST_DIR}" checkout --detach "${PINNED_COMMIT}"

ACTUAL_COMMIT="$(git -C "${DEST_DIR}" rev-parse HEAD)"
if [ "${ACTUAL_COMMIT}" != "${PINNED_COMMIT}" ]; then
  echo "Pinned commit mismatch: expected ${PINNED_COMMIT}, got ${ACTUAL_COMMIT}" >&2
  exit 1
fi

[ -f "${DEST_DIR}/bitcoin.sh" ] || {
  echo "Missing bitcoin.sh at ${DEST_DIR}/bitcoin.sh" >&2
  exit 1
}

ACTUAL_BITCOIN_SH_SHA256="$(sha256sum "${DEST_DIR}/bitcoin.sh")"
ACTUAL_BITCOIN_SH_SHA256="${ACTUAL_BITCOIN_SH_SHA256%% *}"
if [ "${ACTUAL_BITCOIN_SH_SHA256}" != "${PINNED_BITCOIN_SH_SHA256}" ]; then
  echo "Pinned bitcoin.sh SHA256 mismatch: expected ${PINNED_BITCOIN_SH_SHA256}, got ${ACTUAL_BITCOIN_SH_SHA256}" >&2
  exit 1
fi

echo "bitcoin-bash-tools ready at: ${DEST_DIR}"
echo "Pinned commit: ${ACTUAL_COMMIT}"
echo "Pinned bitcoin.sh SHA256: ${ACTUAL_BITCOIN_SH_SHA256}"
