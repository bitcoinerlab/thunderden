#!/bin/sh
set -eu

EXTERNAL_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

EFI_CFG_SRC="${EXTERNAL_DIR}/board/thunderden/grub/grub-efi.cfg"
EFI_BOOT_DIR="${BINARIES_DIR}/efi-part/EFI/BOOT"
EFI_LOADER="${EFI_BOOT_DIR}/bootx64.efi"
EFI_CFG_DST="${EFI_BOOT_DIR}/grub.cfg"

[ -f "${EFI_LOADER}" ] || {
  echo "Missing UEFI loader: ${EFI_LOADER}" >&2
  exit 1
}

install -D -m 0644 "${EFI_CFG_SRC}" "${EFI_CFG_DST}"

install -m 0755 \
  "${EXTERNAL_DIR}/board/thunderden/make-image.sh" \
  "${BINARIES_DIR}/make-image.sh"

{
  cd "${BINARIES_DIR}"
  for f in \
    bzImage \
    boot.img \
    grub.img \
    rootfs.cpio \
    efi-part/EFI/BOOT/bootx64.efi \
    efi-part/EFI/BOOT/grub.cfg; do
    [ -f "$f" ] || {
      echo "Missing image file: ${BINARIES_DIR}/$f" >&2
      exit 1
    }
    sha256sum "$f"
  done

  [ ! -f rootfs.cpio.gz ] || sha256sum rootfs.cpio.gz
} > "${BINARIES_DIR}/thunderden.SHA256SUMS"
