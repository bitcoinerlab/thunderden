#!/bin/bash
set -euo pipefail

PINNED_COMMIT="7fa496aa2004c55f7845a6cbd003007fb40694fc"
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

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
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

echo "bitcoin-bash-tools ready at: ${DEST_DIR}"
echo "Pinned commit: ${ACTUAL_COMMIT}"
