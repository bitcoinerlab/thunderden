#!/bin/sh
set -eu

EXTERNAL_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
REPO_DIR="$(cd "${EXTERNAL_DIR}/.." && pwd)"
SCRIPTS_DIR="${REPO_DIR}/scripts"
BBT_SRC="${THUNDERDEN_BBT_SRC:-${REPO_DIR}/third_party/bitcoin-bash-tools}"
GRUB_CFG_SRC="${EXTERNAL_DIR}/board/thunderden/grub/grub-efi.cfg"

install -d -m 0755 "${TARGET_DIR}/usr/bin"

for s in \
  thunderden_boot.sh \
  thunderden_hardening.sh \
  thunderden_runtime_guard.sh \
  thunderden_scan_qr.sh \
  thunderden_show_qr.sh \
  thunderden_sign_psbt.sh \
  thunderden_tui.sh; do
  install -m 0755 "${SCRIPTS_DIR}/${s}" "${TARGET_DIR}/usr/bin/${s}"
done

if [ ! -x "${TARGET_DIR}/bin/bash" ]; then
  echo "Missing /bin/bash in target rootfs." >&2
  echo "Enable BR2_PACKAGE_BASH and BR2_PACKAGE_BUSYBOX_SHOW_OTHERS." >&2
  exit 1
fi

if [ ! -x "${TARGET_DIR}/usr/bin/zbarcam" ]; then
  echo "Missing /usr/bin/zbarcam in target rootfs." >&2
  echo "Enable BR2_PACKAGE_ZBAR and required toolchain options." >&2
  exit 1
fi

if [ ! -x "${TARGET_DIR}/usr/bin/qrencode" ]; then
  echo "Missing /usr/bin/qrencode in target rootfs." >&2
  echo "Enable BR2_PACKAGE_LIBQRENCODE_TOOLS." >&2
  exit 1
fi

if [ ! -x "${TARGET_DIR}/usr/bin/bitcoind" ]; then
  echo "Missing /usr/bin/bitcoind in target rootfs." >&2
  echo "Enable BR2_PACKAGE_BITCOIN." >&2
  exit 1
fi

if [ ! -x "${TARGET_DIR}/usr/bin/bitcoin-cli" ]; then
  echo "Missing /usr/bin/bitcoin-cli in target rootfs." >&2
  echo "Enable BR2_PACKAGE_BITCOIN." >&2
  exit 1
fi

if [ ! -x "${TARGET_DIR}/usr/bin/openssl" ]; then
  echo "Missing /usr/bin/openssl in target rootfs." >&2
  echo "Enable BR2_PACKAGE_LIBOPENSSL_BIN." >&2
  exit 1
fi

rm -f "${TARGET_DIR}/usr/bin/bitcoin-tx" "${TARGET_DIR}/usr/bin/bitcoin-util"

if [ ! -f "${BBT_SRC}/bitcoin.sh" ]; then
  echo "Missing bitcoin-bash-tools at ${BBT_SRC}" >&2
  echo "Run scripts/fetch_bitcoin_bash_tools.sh first or set THUNDERDEN_BBT_SRC." >&2
  exit 1
fi

install -d -m 0755 "${TARGET_DIR}/opt/bitcoin-bash-tools"
install -m 0644 "${BBT_SRC}/bitcoin.sh" "${TARGET_DIR}/opt/bitcoin-bash-tools/bitcoin.sh"

if [ -f "${BBT_SRC}/LICENSE" ]; then
  install -m 0644 "${BBT_SRC}/LICENSE" "${TARGET_DIR}/opt/bitcoin-bash-tools/LICENSE"
fi

install -D -m 0644 "${GRUB_CFG_SRC}" "${TARGET_DIR}/boot/grub/grub.cfg"

if [ ! -f "${TARGET_DIR}/lib/grub/i386-pc/boot.img" ]; then
  echo "Missing BIOS GRUB stage1 image at /lib/grub/i386-pc/boot.img" >&2
  echo "Enable BR2_TARGET_GRUB2_I386_PC for hybrid BIOS+UEFI image output." >&2
  exit 1
fi

install -D -m 0644 "${TARGET_DIR}/lib/grub/i386-pc/boot.img" "${BINARIES_DIR}/boot.img"
