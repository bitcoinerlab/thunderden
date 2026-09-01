# Dependency Policy and Pins

Thunder Den keeps runtime dependencies intentionally small.

## Runtime programs

- Linux kernel + BusyBox userspace
- Bash
- GNU coreutils (required by `bitcoin-bash-tools`)
- Bitcoin Core (`bitcoind`, `bitcoin-cli`)
- OpenSSL (`openssl` CLI)
- `dc` (from `bc`)
- `libqrencode` tools (`qrencode`)
- Thunder Den QR scanner

The QR scanner uses `zbar`, `libv4l`, JPEG, and zlib libraries. It does not use
the `zbarcam` program.

## Pinned source references

- Buildroot: `2026.05.2`
- Buildroot tarball sha256: `7cd0b79e657b8a1760cef0a68d083265726efe96a17f7f0cb9c10dd6d29b7107`
- Buildroot signing key: `18C7DF2819C1733D822D599EA500D6EE9CB0E540`
- Bitcoin Core: `31.1`
- Bitcoin Core tarball sha256: `50411d5b43c7e4c90099394759eb6c2add6e7c2dbe728840893d638b6fc6afc9`
- Linux kernel: `7.0.14`
- Linux tarball sha256: `de9999b784d2293f00d39c62d8f92a08ab8a54bc4e80ffd250a0c09cb07a0f98`
- Docker builder: Debian `13.6` from snapshot `20260824T000000Z`, with the base OCI image pinned by digest
- `bitcoin-bash-tools`: `7fa496aa2004c55f7845a6cbd003007fb40694fc`
- `bitcoin-bash-tools/bitcoin.sh` sha256: `772d8d38f0cc215000815176deb555733fa22ff59e3154e280d6fb228af9397e`

The authoritative main pins are in `scripts/build/versions.env`. Buildroot
package hash files verify Bitcoin Core, Linux, package sources, and license
files. `BR2_DOWNLOAD_FORCE_CHECK_HASHES=y` makes a missing source hash a build
failure.

## Runtime storage model

- Root filesystem runs from initramfs in RAM.
- Boot runtime guard enforces RAM-only runtime policy before launching TUI.

## Entropy defaults

- Kernel cmdline uses `random.trust_cpu=off random.trust_bootloader=off`.
- Kernel keeps hardware RNG support enabled (including `virtio-rng` for VM tests).
- Thunder Den imports a mnemonic. It does not generate one.

## Build host dependencies (Linux)

Required build + image assembly packages:

- `build-essential bc bison flex cpio file git rsync unzip wget curl`
- `xz-utils tar python3 libncurses-dev gawk patch pkg-config`
- `meson ninja-build cmake`
- `gpg gpg-agent dirmngr`
- `parted`

Admin-path note:

- some hosts require `/usr/sbin:/sbin` in `PATH` for `parted` discovery
- Buildroot provides `mcopy`, `mdir`, and `mkfs.vfat` in `<output-dir>/host`;
  the image helper finds them automatically

## Rules

- Any new runtime dependency must be justified in a review note.
- Keep dependency additions auditable and minimal.
- Prefer shell + existing base system tools over adding new frameworks.
- Keep documented flow Linux-only unless explicitly expanded.
