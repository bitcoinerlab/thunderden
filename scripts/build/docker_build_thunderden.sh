#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Build Thunder Den inside Docker without host bind-mount compilation.

Usage:
  docker_build_thunderden.sh [options]

Options:
  --output-dir <path>        Host directory for artifacts (default: <repo root>)
  --rebuild-image            Rebuild the Docker builder without Docker layer cache
  --clean-output             Delete only the Buildroot output cache before build
  --clean-cache              Delete Docker cache volumes (dl/src/out) before build
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
VERSIONS_FILE="${ROOT_DIR}/scripts/build/versions.env"
[ -f "$VERSIONS_FILE" ] || { echo "Missing build pins: $VERSIONS_FILE" >&2; exit 1; }
. "$VERSIONS_FILE"

CONTAINER_NAME="thunderden-build-run"
OUTPUT_DIR="$ROOT_DIR"
WORK_REPO_DIR="/work/thunderden"
REBUILD_IMAGE=0
CLEAN_OUTPUT=0
CLEAN_CACHE=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --output-dir)
      [ "$#" -ge 2 ] || { echo "--output-dir requires a value" >&2; exit 1; }
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --rebuild-image)
      REBUILD_IMAGE=1
      shift
      ;;
    --clean-output)
      CLEAN_OUTPUT=1
      shift
      ;;
    --clean-cache)
      CLEAN_CACHE=1
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

CACHE_DL_VOL="thunderden-dl"
CACHE_SRC_VOL="thunderden-src"
CACHE_OUT_VOL="thunderden-out"

cleanup() {
  docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
}

remove_volume() {
  if docker volume inspect "$1" >/dev/null 2>&1; then
    docker volume rm "$1" >/dev/null || {
      echo "Unable to remove Docker volume: $1" >&2
      exit 1
    }
  fi
}

trap cleanup EXIT INT TERM

# Remove an old build container before deleting volumes it may still use.
cleanup

if [ "$CLEAN_CACHE" -eq 1 ]; then
  remove_volume "$CACHE_DL_VOL"
  remove_volume "$CACHE_SRC_VOL"
  remove_volume "$CACHE_OUT_VOL"
elif [ "$CLEAN_OUTPUT" -eq 1 ]; then
  remove_volume "$CACHE_OUT_VOL"
fi

docker volume create "$CACHE_DL_VOL" >/dev/null
docker volume create "$CACHE_SRC_VOL" >/dev/null
docker volume create "$CACHE_OUT_VOL" >/dev/null

DOCKER_BUILD_ARGS=(
  --platform "$BUILDER_PLATFORM"
  --build-arg "DEBIAN_BASE_IMAGE=$DEBIAN_BASE_IMAGE"
  --build-arg "DEBIAN_SNAPSHOT=$DEBIAN_SNAPSHOT"
  --tag "$BUILDER_IMAGE"
  --file "$ROOT_DIR/scripts/build/Dockerfile"
)
[ "$REBUILD_IMAGE" -eq 0 ] || DOCKER_BUILD_ARGS+=(--no-cache)
docker build "${DOCKER_BUILD_ARGS[@]}" "$ROOT_DIR"

docker run -d --platform "$BUILDER_PLATFORM" --name "$CONTAINER_NAME" \
  -v "${CACHE_DL_VOL}:/cache/dl" \
  -v "${CACHE_SRC_VOL}:/cache/src" \
  -v "${CACHE_OUT_VOL}:/cache/out" \
  "$BUILDER_IMAGE" sleep infinity >/dev/null

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

docker exec -u builder -e WORK_REPO_DIR="$WORK_REPO_DIR" "$CONTAINER_NAME" bash -lc '
set -euo pipefail
cd "$WORK_REPO_DIR"
. ./scripts/build/versions.env
bbt_cache="/cache/src/bitcoin-bash-tools-${BITCOIN_BASH_TOOLS_COMMIT}.sh"
if [ -f "$bbt_cache" ]; then
  mkdir -p third_party/bitcoin-bash-tools
  install -m 0644 "$bbt_cache" third_party/bitcoin-bash-tools/bitcoin.sh
fi
./scripts/build/fetch_bitcoin_bash_tools.sh
install -m 0644 third_party/bitcoin-bash-tools/bitcoin.sh "$bbt_cache"
mkdir -p /cache/src
cd /cache/src

archive="buildroot-${BUILDROOT_VERSION}.tar.xz"
signature="${archive}.sign"
release_key="buildroot-release-key.asc"
source_dir="buildroot-${BUILDROOT_VERSION}"

if [ ! -f "$archive" ]; then
  curl --fail --location --retry 3 --output "${archive}.tmp" \
    "https://buildroot.org/downloads/${archive}"
  mv "${archive}.tmp" "$archive"
fi
if [ ! -f "$signature" ]; then
  curl --fail --location --retry 3 --output "${signature}.tmp" \
    "https://buildroot.org/downloads/${signature}"
  mv "${signature}.tmp" "$signature"
fi
if [ ! -f "$release_key" ]; then
  curl --fail --location --retry 3 --output "${release_key}.tmp" \
    "$BUILDROOT_SIGNING_KEY_URL"
  mv "${release_key}.tmp" "$release_key"
fi

printf "%s  %s\n" "$BUILDROOT_SHA256" "$archive" | sha256sum -c -

key_fingerprints="$({ gpg --batch --with-colons --show-keys "$release_key" 2>/dev/null || true; } | awk -F: '\''$1 == "fpr" { print $10 }'\'')"
printf "%s\n" "$key_fingerprints" | grep -Fxq "$BUILDROOT_SIGNING_KEY_FINGERPRINT" || {
  echo "Buildroot signing key fingerprint mismatch" >&2
  exit 1
}

export GNUPGHOME
GNUPGHOME="$(mktemp -d)"
trap '\''rm -rf "$GNUPGHOME"'\'' EXIT
gpg --batch --import "$release_key" >/dev/null 2>&1
gpg --batch --verify "$signature"

source_marker="${source_dir}/.thunderden-source-sha256"
source_hash=""
[ ! -f "$source_marker" ] || source_hash="$(< "$source_marker")"
if [ "$source_hash" != "$BUILDROOT_SHA256" ]; then
  rm -rf "$source_dir"
  tar -xf "$archive"
  printf "%s\n" "$BUILDROOT_SHA256" > "$source_marker"
fi

cd "$WORK_REPO_DIR"
export BR2_DL_DIR=/cache/dl
./scripts/build/build_thunderden.sh \
  --buildroot-dir "/cache/src/buildroot-${BUILDROOT_VERSION}" \
  --output-dir /cache/out
'

docker exec -u builder "$CONTAINER_NAME" bash -lc "
set -euo pipefail
cd $WORK_REPO_DIR
IMAGE_HELPER=/cache/out/images/make-image.sh

# Compatibility-first image.
\"\$IMAGE_HELPER\" --binaries-dir /cache/out/images --output thunderden.img
sha256sum thunderden.img > thunderden.img.sha256

# Minimum-size image for x86_64 UEFI systems with very small boot media.
\"\$IMAGE_HELPER\" --binaries-dir /cache/out/images --output thunderden-small.img --small
sha256sum thunderden-small.img > thunderden-small.img.sha256
sha256sum thunderden.img thunderden-small.img > SHA256SUMS

"

docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden.img" "$OUTPUT_DIR/thunderden.img"
docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden.img.sha256" "$OUTPUT_DIR/thunderden.img.sha256"
docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden-small.img" "$OUTPUT_DIR/thunderden-small.img"
docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/thunderden-small.img.sha256" "$OUTPUT_DIR/thunderden-small.img.sha256"
docker cp "$CONTAINER_NAME:$WORK_REPO_DIR/SHA256SUMS" "$OUTPUT_DIR/SHA256SUMS"
docker cp "$CONTAINER_NAME:/cache/out/images/thunderden.SHA256SUMS" "$OUTPUT_DIR/thunderden.SHA256SUMS"

echo
echo "Docker build complete."
echo "Artifacts:"
echo "  $OUTPUT_DIR/thunderden.img (max compatibility)"
echo "  $OUTPUT_DIR/thunderden.img.sha256"
echo "  $OUTPUT_DIR/thunderden-small.img (minimum-size x86_64 UEFI only, no GRUB)"
echo "  $OUTPUT_DIR/thunderden-small.img.sha256"
echo "  $OUTPUT_DIR/SHA256SUMS"
echo "  $OUTPUT_DIR/thunderden.SHA256SUMS"
