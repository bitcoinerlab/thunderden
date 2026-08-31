#!/bin/sh
set -eu

EXTERNAL_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
REPO_DIR="$(cd "${EXTERNAL_DIR}/.." && pwd)"
SCRIPTS_DIR="${REPO_DIR}/scripts/runtime"
BBT_SRC="${THUNDERDEN_BBT_SRC:-${REPO_DIR}/third_party/bitcoin-bash-tools}"

install -d -m 0755 "${TARGET_DIR}/usr/bin"

for s in \
  thunderden_boot.sh \
  thunderden_export_bip84_descriptor.sh \
  thunderden_runtime_guard.sh \
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

if [ ! -x "${TARGET_DIR}/usr/bin/thunderden-qrscan" ]; then
  echo "Missing /usr/bin/thunderden-qrscan in target rootfs." >&2
  echo "Enable BR2_PACKAGE_THUNDERDEN_QRSCAN." >&2
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

# These programs are built by required packages but are not used at runtime.
rm -f \
  "${TARGET_DIR}/usr/bin/bc" \
  "${TARGET_DIR}/usr/bin/bitcoin-tx" \
  "${TARGET_DIR}/usr/bin/bitcoin-util" \
  "${TARGET_DIR}/usr/bin/fbv" \
  "${TARGET_DIR}/usr/bin/v4l2grab" \
  "${TARGET_DIR}/usr/bin/zbarcam" \
  "${TARGET_DIR}/usr/bin/zbarimg"

# Thunder Den is built for initramfs-only runtime without kernel modules.
# Remove any stale module trees left by incremental builds.
rm -rf "${TARGET_DIR}/lib/modules"

if [ ! -f "${BBT_SRC}/bitcoin.sh" ]; then
  echo "Missing bitcoin-bash-tools at ${BBT_SRC}" >&2
  echo "Run scripts/build/fetch_bitcoin_bash_tools.sh first or set THUNDERDEN_BBT_SRC." >&2
  exit 1
fi

install -d -m 0755 "${TARGET_DIR}/opt/bitcoin-bash-tools"
install -m 0644 "${BBT_SRC}/bitcoin.sh" "${TARGET_DIR}/opt/bitcoin-bash-tools/bitcoin.sh"

if [ -f "${BBT_SRC}/LICENSE" ]; then
  install -m 0644 "${BBT_SRC}/LICENSE" "${TARGET_DIR}/opt/bitcoin-bash-tools/LICENSE"
fi

BOOT_IMG_SRC=""

if [ -f "${TARGET_DIR}/lib/grub/i386-pc/boot.img" ]; then
  BOOT_IMG_SRC="${TARGET_DIR}/lib/grub/i386-pc/boot.img"
else
  for candidate in "${BASE_DIR}"/build/grub2-*/build-i386-pc/grub-core/boot.img; do
    if [ -f "${candidate}" ]; then
      BOOT_IMG_SRC="${candidate}"
      break
    fi
  done
fi

if [ -z "${BOOT_IMG_SRC}" ]; then
  echo "Missing BIOS GRUB stage1 image (boot.img)." >&2
  echo "Checked TARGET_DIR and Buildroot grub2 build output paths." >&2
  echo "Enable BR2_TARGET_GRUB2_I386_PC for hybrid BIOS+UEFI image output." >&2
  exit 1
fi

install -D -m 0644 "${BOOT_IMG_SRC}" "${BINARIES_DIR}/boot.img"
