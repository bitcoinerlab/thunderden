#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Build Thunder Den inside Docker without host bind-mount compilation.

Usage:
  docker_build_thunderden.sh [options]

Options:
  --buildroot-version <ver>  Buildroot release version (default: 2025.11.1)
  --builder-image <name>     Docker image used for builds (default: thunderden-builder:debian12)
  --container-name <name>    Container name for current run (default: thunderden-build-run)
  --cache-prefix <prefix>    Prefix for Docker cache volumes (default: thunderden)
  --output-dir <path>        Host directory for artifacts (default: <repo root>)
  --rebuild-image            Force rebuild of Docker builder image
  --clean-cache              Delete Docker cache volumes (dl/src/out) before build
  --keep-container           Keep container after script exits
  -h, --help                 Show this help

Notes:
  - Designed for macOS, Linux, and Windows users running WSL2 with Docker.
  - Source is copied into the container; build is done on container-local fs.
  - Build caches (downloads, sources, build output) are stored in Docker
    volumes for reuse across runs.
  - Final image assembly is rootless (no loop devices / no privileged mode).
EOF
}

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILDROOT_VERSION="2025.11.1"
BUILDER_IMAGE="thunderden-builder:debian12"
CONTAINER_NAME="thunderden-build-run"
CACHE_PREFIX="thunderden"
OUTPUT_DIR="$ROOT_DIR"
WORK_REPO_DIR="/work/thunderden"
REBUILD_IMAGE=0
CLEAN_CACHE=0
KEEP_CONTAINER=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --buildroot-version)
      [ "$#" -ge 2 ] || { echo "--buildroot-version requires a value" >&2; exit 1; }
      BUILDROOT_VERSION="$2"
      shift 2
      ;;
    --builder-image)
      [ "$#" -ge 2 ] || { echo "--builder-image requires a value" >&2; exit 1; }
      BUILDER_IMAGE="$2"
      shift 2
      ;;
    --container-name)
      [ "$#" -ge 2 ] || { echo "--container-name requires a value" >&2; exit 1; }
      CONTAINER_NAME="$2"
      shift 2
      ;;
    --cache-prefix)
      [ "$#" -ge 2 ] || { echo "--cache-prefix requires a value" >&2; exit 1; }
      CACHE_PREFIX="$2"
      shift 2
      ;;
    --output-dir)
      [ "$#" -ge 2 ] || { echo "--output-dir requires a value" >&2; exit 1; }
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --rebuild-image)
      REBUILD_IMAGE=1
      shift
      ;;
    --clean-cache)
      CLEAN_CACHE=1
      shift
      ;;
    --keep-container)
      KEEP_CONTAINER=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 1
  }
}

ensure_supported_host_shell() {
  local host_uname=""

  host_uname="$(uname -s)"
  case "$host_uname" in
    Darwin|Linux)
      ;;
    MINGW*|MSYS*|CYGWIN*)
      echo "Unsupported shell: $host_uname" >&2
      echo "Use WSL2 (Linux shell) on Windows." >&2
      exit 1
      ;;
    *)
      echo "Unsupported host OS: $host_uname" >&2
      exit 1
      ;;
  esac

  if [ "$host_uname" = "Linux" ] && [ -f /proc/sys/kernel/osrelease ]; then
    if grep -qi microsoft /proc/sys/kernel/osrelease && ! grep -qi wsl2 /proc/sys/kernel/osrelease; then
      echo "WSL1 detected. Use WSL2 for supported Docker workflow." >&2
      exit 1
    fi
  fi
}

ensure_supported_host_shell

need_cmd docker
need_cmd tar

docker info >/dev/null 2>&1 || {
  echo "Docker daemon is not available." >&2
  exit 1
}

[ -f "$ROOT_DIR/scripts/build/build_thunderden.sh" ] || {
  echo "Repository layout check failed: scripts/build/build_thunderden.sh not found" >&2
  exit 1
}

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

CACHE_DL_VOL="${CACHE_PREFIX}-dl"
CACHE_SRC_VOL="${CACHE_PREFIX}-src"
CACHE_OUT_VOL="${CACHE_PREFIX}-out"

cleanup() {
  if [ "$KEEP_CONTAINER" -eq 0 ]; then
    docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT INT TERM

if [ "$CLEAN_CACHE" -eq 1 ]; then
  docker volume rm "$CACHE_DL_VOL" "$CACHE_SRC_VOL" "$CACHE_OUT_VOL" >/dev/null 2>&1 || true
fi

docker volume create "$CACHE_DL_VOL" >/dev/null
docker volume create "$CACHE_SRC_VOL" >/dev/null
docker volume create "$CACHE_OUT_VOL" >/dev/null

if [ "$REBUILD_IMAGE" -eq 1 ] || ! docker image inspect "$BUILDER_IMAGE" >/dev/null 2>&1; then
  docker build -t "$BUILDER_IMAGE" - <<'EOF'
FROM debian:12

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
  sudo ca-certificates \
  build-essential bc bison flex cpio file git rsync unzip wget curl \
  xz-utils tar python3 libncurses-dev gawk patch pkg-config \
  meson ninja-build cmake gpg gpg-agent dirmngr \
  parted dosfstools util-linux e2fsprogs \
  && rm -rf /var/lib/apt/lists/* \
  && useradd -m -s /bin/bash builder
EOF
fi

docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

docker run -d --name "$CONTAINER_NAME" \
  -v "${CACHE_DL_VOL}:/cache/dl" \
  -v "${CACHE_SRC_VOL}:/cache/src" \
  -v "${CACHE_OUT_VOL}:/cache/out" \
  "$BUILDER_IMAGE" sleep infinity >/dev/null

docker exec "$CONTAINER_NAME" bash -lc 'id -u builder >/dev/null 2>&1 || useradd -m -s /bin/bash builder'

docker exec "$CONTAINER_NAME" bash -lc '
set -e
rm -rf "'"$WORK_REPO_DIR"'"
mkdir -p "'"$WORK_REPO_DIR"'" /cache/dl /cache/src /cache/out
chown -R builder:builder "'"$WORK_REPO_DIR"'" /cache/dl /cache/src /cache/out
'

tar -C "$ROOT_DIR" \
  --exclude='./.git' \
  --exclude='./out' \
  --exclude='./third_party' \
  --exclude='./*.img' \
  --exclude='./*.img.sha256' \
  --exclude='./SHA256SUMS' \
  --exclude='./SHA256SUMS.asc' \
  -cf - . | docker exec -i -u builder "$CONTAINER_NAME" tar -C "$WORK_REPO_DIR" -xf -

docker exec -u builder "$CONTAINER_NAME" bash -lc "
set -euo pipefail
cd $WORK_REPO_DIR
./scripts/build/fetch_bitcoin_bash_tools.sh
mkdir -p /cache/src
cd /cache/src
[ -f buildroot-${BUILDROOT_VERSION}.tar.xz ] || curl -LO https://buildroot.org/downloads/buildroot-${BUILDROOT_VERSION}.tar.xz
[ -d buildroot-${BUILDROOT_VERSION} ] || tar -xf buildroot-${BUILDROOT_VERSION}.tar.xz
cd $WORK_REPO_DIR
export BR2_DL_DIR=/cache/dl
./scripts/build/build_thunderden.sh --buildroot-dir /cache/src/buildroot-${BUILDROOT_VERSION} --output-dir /cache/out
"

docker exec "$CONTAINER_NAME" bash -lc "
set -euo pipefail
cd $WORK_REPO_DIR
IMAGE_HELPER=$WORK_REPO_DIR/buildroot-external/board/thunderden/make-image.sh

# Compatibility-first image.
\"\$IMAGE_HELPER\" --binaries-dir /cache/out/images --output thunderden.img
sha256sum thunderden.img > thunderden.img.sha256
sha256sum -c thunderden.img.sha256

# Tiny image (smallest current FAT16 fit, UEFI-only).
\"\$IMAGE_HELPER\" --binaries-dir /cache/out/images --output thunderden-small.img --small
sha256sum thunderden-small.img > thunderden-small.img.sha256
sha256sum -c thunderden-small.img.sha256
"

docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden.img" "$OUTPUT_DIR/thunderden.img"
docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden.img.sha256" "$OUTPUT_DIR/thunderden.img.sha256"
docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden-small.img" "$OUTPUT_DIR/thunderden-small.img"
docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden-small.img.sha256" "$OUTPUT_DIR/thunderden-small.img.sha256"
docker cp "$CONTAINER_NAME:/cache/out/images/thunderden.SHA256SUMS" "$OUTPUT_DIR/thunderden.SHA256SUMS" >/dev/null 2>&1 || true

echo
echo "Docker build complete."
echo "Artifacts:"
echo "  $OUTPUT_DIR/thunderden.img (max compatibility)"
echo "  $OUTPUT_DIR/thunderden.img.sha256"
echo "  $OUTPUT_DIR/thunderden-small.img (smallest current payload fit, UEFI-only)"
echo "  $OUTPUT_DIR/thunderden-small.img.sha256"
if [ -f "$OUTPUT_DIR/thunderden.SHA256SUMS" ]; then
  echo "  $OUTPUT_DIR/thunderden.SHA256SUMS"
fi
