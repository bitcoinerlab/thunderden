#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

check_dir() {
  local dir="$1"
  if [ -d "$dir" ]; then
    while IFS= read -r file; do
      bash -n "$file"
    done < <(find "$dir" -type f -name '*.sh' | sort)
  fi
}

check_dir "${ROOT_DIR}/scripts"
check_dir "${ROOT_DIR}/buildroot-external"

bash -n "${ROOT_DIR}/entropy2Mnemonic.bash"
bash -n "${ROOT_DIR}/makeRandom.bash"

echo "All shell syntax checks passed."
