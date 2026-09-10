#!/bin/sh
set -eu
set -a
. /work/.env
set +a
export TZ=UTC LC_ALL=C

archive="/cache/src/buildroot-$BUILDROOT_VERSION.tar.xz"
if [ ! -f "$archive" ]; then
    curl --fail --location --retry 3 "https://buildroot.org/downloads/buildroot-$BUILDROOT_VERSION.tar.xz" -o "$archive"
fi
printf '%s  %s\n' "$BUILDROOT_SHA256" "$archive" | sha256sum -c -
curl --fail --location --retry 3 "https://buildroot.org/downloads/buildroot-$BUILDROOT_VERSION.tar.xz.sign" -o /tmp/buildroot.sign
curl --fail --location --retry 3 "$BUILDROOT_SIGNING_KEY_URL" -o /tmp/buildroot-key.asc
export GNUPGHOME=/tmp/buildroot-gnupg
mkdir -m 0700 "$GNUPGHOME"
gpg --batch --import /tmp/buildroot-key.asc
gpg --batch --export "$BUILDROOT_SIGNING_KEY_FINGERPRINT" > /tmp/buildroot-trusted.gpg
gpgv --keyring /tmp/buildroot-trusted.gpg --output /tmp/buildroot-checksums /tmp/buildroot.sign
grep -Fx "SHA256: $BUILDROOT_SHA256  buildroot-$BUILDROOT_VERSION.tar.xz" /tmp/buildroot-checksums

tar -xf "$archive" -C /tmp
source="/tmp/buildroot-$BUILDROOT_VERSION"
cp /work/platform/defconfig /tmp/thunderden_defconfig
printf 'BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="%s"\n' "$LINUX_VERSION" >> /tmp/thunderden_defconfig
mkdir -p "/tmp/thunderden-hashes/linux/$LINUX_VERSION"
printf 'sha256  %s  linux-%s.tar.xz\n' "$LINUX_SOURCE_SHA256" "$LINUX_VERSION" \
    > "/tmp/thunderden-hashes/linux/$LINUX_VERSION/linux.hash"
export BR2_DL_DIR=/cache/dl
make -C "$source" O=/cache/out BR2_EXTERNAL=/work/platform BR2_DEFCONFIG=/tmp/thunderden_defconfig defconfig
make -C "$source" O=/cache/out thunderden-dirclean
make -C "$source" O=/cache/out
python3 /work/tests/image_contents.py /cache/out
python3 /work/build/image.py /cache/out/images /work/platform/grub.cfg
python3 /work/tests/image_assembly.py /cache/out/images /work/platform/grub.cfg
