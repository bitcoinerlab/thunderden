#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

check_dir() {
  local dir="$1"
  local first_line=""
  if [ -d "$dir" ]; then
    while IFS= read -r file; do
      IFS= read -r first_line < "$file" || true
      if [ "$first_line" = '#!/bin/sh' ]; then
        sh -n "$file"
      else
        bash -n "$file"
      fi
    done < <(find "$dir" -type f \( -name '*.sh' -o -name '*.bash' \) | sort)
  fi
}

check_dir "${ROOT_DIR}/scripts"
check_dir "${ROOT_DIR}/buildroot-external"
bash -n "${ROOT_DIR}/scripts/build/versions.env"
sh -n "${ROOT_DIR}/buildroot-external/board/thunderden/rootfs-overlay/etc/init.d/S11thunderden-runtime"

echo "All shell syntax checks passed."
