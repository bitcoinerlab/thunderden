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

- Buildroot: `2025.11.1`
- Bitcoin Core: `30.2` (pinned by `scripts/build/build_thunderden.sh` + `buildroot-external/patches/bitcoin/30.2/bitcoin.hash`)
- `bitcoin-bash-tools`: `7fa496aa2004c55f7845a6cbd003007fb40694fc`
- `bitcoin-bash-tools/bitcoin.sh` sha256: `772d8d38f0cc215000815176deb555733fa22ff59e3154e280d6fb228af9397e`
- Linux kernel: Buildroot latest for pinned release (2025.11.1 currently tracks `6.18`)

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
