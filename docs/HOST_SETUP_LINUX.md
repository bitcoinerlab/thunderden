# Linux Build Host Setup (From Scratch)

Thunder Den build support is Linux-first. Buildroot itself is Linux-focused,
so this project documents Linux host setup only.

## Supported host profile

- Debian 12 or Debian 13 VM on any machine
- Non-root build user with `sudo` access
- Fresh workspace dedicated to Thunder Den builds

## Install required host packages

These are the minimal packages needed to build and assemble
`thunderden.img` / `thunderden-small.img`:

```bash
sudo apt update
sudo apt install -y \
  build-essential bc bison flex cpio file git rsync unzip wget curl \
  xz-utils tar python3 libncurses-dev gawk patch pkg-config \
  meson ninja-build cmake \
  gpg gpg-agent dirmngr \
  parted dosfstools
```

## Preflight checks

```bash
for cmd in \
  git make gcc gpg curl tar xz \
  parted mkfs.vfat; do
  command -v "$cmd" >/dev/null || echo "missing: $cmd"
done
```

If any command is missing, install the corresponding package first.

`mcopy` is built by Buildroot into `out/buildroot/host/bin` during normal builds,
and the Thunder Den image assembly helper auto-discovers that path.

## PATH gotcha for admin tools

On some minimal Debian shells, `parted` and `mkfs.vfat` are under
`/usr/sbin` and are not present in the default non-root `PATH`.

Use this before running image assembly scripts:

```bash
export PATH="/usr/sbin:/sbin:$PATH"
```

The Thunder Den image assembly helper script also prepends these paths
automatically.
