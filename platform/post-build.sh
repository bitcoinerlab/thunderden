#!/bin/sh
set -eu
target="$1"
chmod 0755 "$target/usr/bin/thunderden-session"
# Init executes only the explicit inittab mount commands and signer session.
rm -rf "$target/etc/init.d"
set -- "$BASE_DIR"/build/grub2-*/build-i386-pc/grub-core/boot.img
[ "$#" -eq 1 ] && [ -f "$1" ] || { printf 'Expected one BIOS GRUB stage-one image\n' >&2; exit 1; }
install -m 0644 "$1" "$BINARIES_DIR/boot.img"
