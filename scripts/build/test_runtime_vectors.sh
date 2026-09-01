#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
VERSIONS_FILE="${ROOT_DIR}/scripts/build/versions.env"
[ -f "$VERSIONS_FILE" ] || { echo "Missing build pins: $VERSIONS_FILE" >&2; exit 1; }
. "$VERSIONS_FILE"

BUILDROOT_OUT_VOLUME="thunderden-out"
RUNTIME_IMAGE="thunderden-runtime-test:rootfs-v1"
VECTORS_DIR="${ROOT_DIR}/test-vectors/qr-signing"
TMP_DIR=""

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Missing command: $1"
}

print_file_stderr() {
  local file="$1"
  local line=""

  while IFS= read -r line || [ -n "$line" ]; do
    printf '%s\n' "$line" >&2
  done < "$file"
}

cleanup() {
  if [ -n "$TMP_DIR" ] && [ -d "$TMP_DIR" ]; then
    rm -rf "$TMP_DIR"
  fi
}

trap cleanup EXIT INT TERM

need_cmd cmp
need_cmd docker
need_cmd grep
need_cmd mktemp

docker info >/dev/null 2>&1 || die "Docker daemon is not available."
docker image inspect "$BUILDER_IMAGE" >/dev/null 2>&1 ||
  die "Missing $BUILDER_IMAGE. Run scripts/build/docker_build_thunderden.sh first."
docker volume inspect "$BUILDROOT_OUT_VOLUME" >/dev/null 2>&1 ||
  die "Missing $BUILDROOT_OUT_VOLUME. Run scripts/build/docker_build_thunderden.sh first."

for script in \
  thunderden_export_bip84_descriptor.sh \
  thunderden_show_qr.sh \
  thunderden_sign_psbt.sh; do
  [ -x "${ROOT_DIR}/scripts/runtime/${script}" ] ||
    die "Missing executable runtime script: scripts/runtime/${script}"
done

[ -f "${VECTORS_DIR}/index.json" ] || die "Missing QR signing vector index."

ROOTFS_INFO="$(
  docker run --rm --pull=never --platform linux/amd64 \
    --network none \
    --read-only \
    --mount "type=volume,src=${BUILDROOT_OUT_VOLUME},dst=/out,readonly" \
    "$BUILDER_IMAGE" sh -ec '
      test -f /out/.config
      grep -qx "BR2_x86_64=y" /out/.config
      test -s /out/images/rootfs.cpio
      sha256sum /out/images/rootfs.cpio
    '
)" || die "The Docker Buildroot cache does not contain a completed Thunder Den rootfs."

read -r ROOTFS_SHA _ <<< "$ROOTFS_INFO"
[ "${#ROOTFS_SHA}" -eq 64 ] || die "Unable to determine the generated rootfs checksum."
case "$ROOTFS_SHA" in
  *[!0-9a-f]*) die "Invalid generated rootfs checksum: $ROOTFS_SHA" ;;
esac

CACHED_ROOTFS_SHA="$(
  docker image inspect \
    --format '{{ index .Config.Labels "org.thunderden.rootfs-sha256" }}' \
    "$RUNTIME_IMAGE" 2>/dev/null || true
)"

if [ "$CACHED_ROOTFS_SHA" != "$ROOTFS_SHA" ]; then
  printf 'Preparing runtime test image from generated Buildroot rootfs...\n'

  if docker image inspect "$RUNTIME_IMAGE" >/dev/null 2>&1; then
    docker image rm "$RUNTIME_IMAGE" >/dev/null ||
      die "Unable to replace stale runtime test image: $RUNTIME_IMAGE"
  fi

  if ! docker run --rm --pull=never --platform linux/amd64 \
      --network none \
      --read-only \
      --tmpfs /tmp:rw,exec,mode=1777 \
      --mount "type=volume,src=${BUILDROOT_OUT_VOLUME},dst=/out,readonly" \
      "$BUILDER_IMAGE" sh -ec '
        mkdir /tmp/rootfs
        cd /tmp/rootfs
        cpio --extract --make-directories --preserve-modification-time --quiet \
          < /out/images/rootfs.cpio
        tar --numeric-owner -cf - .
      ' |
    docker import --platform linux/amd64 \
      --change 'ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' \
      --change "LABEL org.thunderden.rootfs-sha256=${ROOTFS_SHA}" \
      - "$RUNTIME_IMAGE" >/dev/null; then
    die "Unable to prepare the runtime test image."
  fi
fi

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/thunderden-runtime-test.XXXXXX")"
MANIFEST="${TMP_DIR}/vectors.tsv"

docker run -i --rm --pull=never --platform linux/amd64 \
  --network none \
  --read-only \
  --env PYTHONDONTWRITEBYTECODE=1 \
  --mount "type=bind,src=${VECTORS_DIR},dst=/vectors,readonly" \
  "$BUILDER_IMAGE" python3 - /vectors/index.json > "$MANIFEST" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

index_path = Path(sys.argv[1])
base_dir = index_path.parent
index = json.loads(index_path.read_text(encoding="utf-8"))
seen = set()

for vector in index.get("vectors", []):
    vector_id = vector["id"]
    if vector_id in seen:
        raise SystemExit(f"Duplicate vector id: {vector_id}")
    seen.add(vector_id)

    unsigned_path = base_dir / vector["unsigned_psbt_file"]
    signed_path = base_dir / vector["signed_psbt_file"]
    metadata_path = base_dir / vector["metadata_file"]
    for path in (unsigned_path, signed_path, metadata_path):
        if not path.is_file():
            raise SystemExit(f"Missing vector file: {path}")

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if metadata["id"] != vector_id:
        raise SystemExit(f"Metadata id mismatch for {vector_id}")
    if metadata["network"] != vector["network"]:
        raise SystemExit(f"Metadata network mismatch for {vector_id}")

    unsigned = unsigned_path.read_text(encoding="ascii").strip()
    if not unsigned.startswith("cHNidP"):
        raise SystemExit(f"Invalid unsigned PSBT prefix for {vector_id}")
    digest = hashlib.sha256(unsigned.encode("ascii")).hexdigest()
    if digest != metadata["unsigned_psbt_sha256"]:
        raise SystemExit(f"Unsigned PSBT checksum mismatch for {vector_id}")

    fields = (
        vector_id,
        vector["network"],
        vector["unsigned_psbt_file"],
        vector["signed_psbt_file"],
        metadata["mnemonic"],
        metadata["account_path"],
        metadata["master_fingerprint_hex"],
        metadata.get("passphrase", ""),
    )
    if any("\t" in value or "\n" in value for value in fields):
        raise SystemExit(f"Unsupported tab or newline in metadata for {vector_id}")
    print("\t".join(fields))

if not seen:
    raise SystemExit("No vectors found")
PY

[ -s "$MANIFEST" ] || die "The QR signing vector index contains no vectors."

RUNTIME_ARGS=(
  --rm
  --pull=never
  --platform linux/amd64
  --network none
  --read-only
  --cap-drop ALL
  --cap-add NET_ADMIN
  --security-opt no-new-privileges
  --tmpfs /tmp:rw,nosuid,nodev,mode=1777
  --tmpfs /run:rw,nosuid,nodev,mode=0755
  --mount "type=bind,src=${VECTORS_DIR},dst=/vectors,readonly"
  --mount "type=bind,src=${ROOT_DIR}/scripts/runtime/thunderden_export_bip84_descriptor.sh,dst=/usr/bin/thunderden_export_bip84_descriptor.sh,readonly"
  --mount "type=bind,src=${ROOT_DIR}/scripts/runtime/thunderden_show_qr.sh,dst=/usr/bin/thunderden_show_qr.sh,readonly"
  --mount "type=bind,src=${ROOT_DIR}/scripts/runtime/thunderden_sign_psbt.sh,dst=/usr/bin/thunderden_sign_psbt.sh,readonly"
)

VECTOR_COUNT=0
while IFS=$'\t' read -r \
    VECTOR_ID NETWORK UNSIGNED_REL SIGNED_REL MNEMONIC ACCOUNT_PATH FINGERPRINT PASSPHRASE; do
  [ -n "$VECTOR_ID" ] || continue
  VECTOR_COUNT=$((VECTOR_COUNT + 1))

  printf '[%s] signing...\n' "$VECTOR_ID"
  SIGNED_OUT="${TMP_DIR}/${VECTOR_ID}.signed.txt"
  SIGNER_ERR="${TMP_DIR}/${VECTOR_ID}.signer.err"
  if ! printf '%s\n' "$MNEMONIC" |
      docker run -i "${RUNTIME_ARGS[@]}" \
        --env "BIP39_PASSPHRASE=${PASSPHRASE}" \
        "$RUNTIME_IMAGE" \
        /usr/bin/thunderden_sign_psbt.sh \
          --network "$NETWORK" \
          --psbt-file "/vectors/${UNSIGNED_REL}" \
        > "$SIGNED_OUT" 2> "$SIGNER_ERR"; then
    print_file_stderr "$SIGNER_ERR"
    die "Signer failed for $VECTOR_ID"
  fi

  grep -qx 'complete=true' "$SIGNER_ERR" || {
    print_file_stderr "$SIGNER_ERR"
    die "Signer did not complete every input for $VECTOR_ID"
  }
  cmp -s "$SIGNED_OUT" "${VECTORS_DIR}/${SIGNED_REL}" ||
    die "Signed PSBT differs from the expected output for $VECTOR_ID"

  printf '[%s] descriptor...\n' "$VECTOR_ID"
  DESCRIPTOR_OUT="${TMP_DIR}/${VECTOR_ID}.descriptor.txt"
  DESCRIPTOR_ERR="${TMP_DIR}/${VECTOR_ID}.descriptor.err"
  if ! printf '%s\n' "$MNEMONIC" |
      docker run -i "${RUNTIME_ARGS[@]}" \
        --env "BIP39_PASSPHRASE=${PASSPHRASE}" \
        "$RUNTIME_IMAGE" \
        /usr/bin/thunderden_export_bip84_descriptor.sh \
          --network "$NETWORK" \
          --no-qr \
        > "$DESCRIPTOR_OUT" 2> "$DESCRIPTOR_ERR"; then
    print_file_stderr "$DESCRIPTOR_ERR"
    die "Descriptor export failed for $VECTOR_ID"
  fi

  if [ -s "$DESCRIPTOR_ERR" ]; then
    print_file_stderr "$DESCRIPTOR_ERR"
    die "Descriptor export wrote unexpected diagnostics for $VECTOR_ID"
  fi

  grep -Fqx "Network: ${NETWORK}" "$DESCRIPTOR_OUT" ||
    die "Descriptor network mismatch for $VECTOR_ID"
  grep -Fqx "Account path: ${ACCOUNT_PATH}" "$DESCRIPTOR_OUT" ||
    die "Descriptor account path mismatch for $VECTOR_ID"
  grep -Fqx "Master fingerprint: ${FINGERPRINT}" "$DESCRIPTOR_OUT" ||
    die "Descriptor fingerprint mismatch for $VECTOR_ID"
  if grep -Eq '(xprv|tprv)' "$DESCRIPTOR_OUT"; then
    die "Descriptor export exposed a private key for $VECTOR_ID"
  fi

  if [ "$NETWORK" = "main" ]; then
    XPUB_PREFIX="xpub"
  else
    XPUB_PREFIX="tpub"
  fi

  ACCOUNT_XPUB_LINE="$(grep '^Account xpub: ' "$DESCRIPTOR_OUT" || true)"
  case "$ACCOUNT_XPUB_LINE" in
    "Account xpub: ${XPUB_PREFIX}"*) ;;
    *) die "Descriptor account key prefix mismatch for $VECTOR_ID" ;;
  esac

  DESCRIPTOR_LINE="$(grep '^wpkh(' "$DESCRIPTOR_OUT" || true)"
  DESCRIPTOR_PREFIX="wpkh([${FINGERPRINT}/${ACCOUNT_PATH#m/}]${XPUB_PREFIX}"
  case "$DESCRIPTOR_LINE" in
    "${DESCRIPTOR_PREFIX}"*"/<0;1>/*)") ;;
    *) die "Descriptor structure mismatch for $VECTOR_ID" ;;
  esac

  printf '[%s] QR rendering...\n' "$VECTOR_ID"
  QR_OUT="${TMP_DIR}/${VECTOR_ID}.qr.txt"
  QR_ERR="${TMP_DIR}/${VECTOR_ID}.qr.err"
  if ! docker run -i "${RUNTIME_ARGS[@]}" \
      "$RUNTIME_IMAGE" /usr/bin/thunderden_show_qr.sh \
      < "$SIGNED_OUT" > "$QR_OUT" 2> "$QR_ERR"; then
    print_file_stderr "$QR_ERR"
    die "QR rendering failed for $VECTOR_ID"
  fi

  if [ -s "$QR_ERR" ]; then
    print_file_stderr "$QR_ERR"
    die "QR rendering wrote unexpected diagnostics for $VECTOR_ID"
  fi
  [ -s "$QR_OUT" ] || die "QR rendering produced no output for $VECTOR_ID"
  grep -Fq 'Mode: auto stream | chunk=' "$QR_OUT" ||
    die "QR rendering did not use the expected stream profile for $VECTOR_ID"
done < "$MANIFEST"

printf 'Runtime vector tests passed: %s vectors.\n' "$VECTOR_COUNT"
