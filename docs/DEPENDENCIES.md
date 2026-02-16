# Dependency Policy and Pins

Thunder Den keeps runtime dependencies intentionally small.

## Runtime dependencies (v1)

- Linux kernel + BusyBox userspace
- Bash
- Bitcoin Core (`bitcoind`, `bitcoin-cli`)
- OpenSSL
- `dc` (from `bc`)
- `libqrencode` tools (`qrencode`)
- `zbar` (`zbarcam`)
- `kmod` (`modprobe`)

## Pinned source references

- Buildroot: `2025.02.10`
- Bitcoin Core release line: `30.2` (tracked in release verification records)
- `bitcoin-bash-tools`: `7fa496aa2004c55f7845a6cbd003007fb40694fc`
- `bitcoin-bash-tools/bitcoin.sh` sha256: `772d8d38f0cc215000815176deb555733fa22ff59e3154e280d6fb228af9397e`
- Linux kernel version in defconfig: `6.12.65`

## Runtime storage model

- Root filesystem runs from initramfs in RAM.
- Boot runtime guard enforces RAM-only runtime policy before launching TUI.

## Build host dependencies (Linux)

Required build + image assembly packages:

- `build-essential bc bison flex cpio file git rsync unzip wget curl`
- `xz-utils tar python3 libncurses-dev gawk patch pkg-config`
- `meson ninja-build cmake`
- `gpg gpg-agent dirmngr`
- `parted dosfstools`

Optional (local image boot testing only):

- `qemu-system-x86 ovmf`

Admin-path note:

- some hosts require `/usr/sbin:/sbin` in `PATH` for `parted`/`mkfs.vfat` discovery
- `mcopy` is provided by Buildroot host tools (`<output-dir>/host/bin`) and the image
  helper prepends that path automatically when available

## Rules

- Any new runtime dependency must be justified in a review note.
- Keep dependency additions auditable and minimal.
- Prefer shell + existing base system tools over adding new frameworks.
- Keep documented flow Linux-only unless explicitly expanded.
