#!/bin/sh
set -eu

EXTERNAL_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

EFI_CFG_SRC="${EXTERNAL_DIR}/board/thunderden/grub/grub-efi.cfg"
EFI_CFG_DST="${BINARIES_DIR}/efi-part/EFI/BOOT/grub.cfg"

if [ -f "${EFI_CFG_DST}" ]; then
  install -D -m 0644 "${EFI_CFG_SRC}" "${EFI_CFG_DST}"
fi

if [ -d "${BINARIES_DIR}/efi-part" ] && [ -f "${BINARIES_DIR}/bzImage" ]; then
  cp -f "${BINARIES_DIR}/bzImage" "${BINARIES_DIR}/efi-part/bzImage"
fi

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
    rootfs.cpio.gz \
    efi-part/EFI/BOOT/bootx64.efi \
    efi-part/EFI/BOOT/grub.cfg; do
    if [ -f "$f" ]; then
      sha256sum "$f"
    fi
  done
} > "${BINARIES_DIR}/thunderden.SHA256SUMS"
