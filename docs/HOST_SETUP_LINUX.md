# Linux Build Host Setup (From Scratch)

Thunder Den build support is Linux-first. Buildroot itself is Linux-focused,
so this project documents Linux host setup only.

## Supported host profile

- Debian 12 or Debian 13 VM on any machine
- Non-root build user with `sudo` access
- Fresh workspace dedicated to Thunder Den builds

## Install required host packages

These are the minimal packages needed to build and assemble
`thunderden-uefi.img`:

```bash
sudo apt update
sudo apt install -y \
  build-essential bc bison flex cpio file git rsync unzip wget curl \
  xz-utils tar python3 libncurses-dev gawk patch pkg-config \
  meson ninja-build cmake \
  gpg gpg-agent dirmngr \
  parted dosfstools util-linux
```

## Optional packages (local VM image boot test)

Install only if you want to run local QEMU boot tests on the build host:

```bash
sudo apt install -y qemu-system-x86 ovmf
```

## Preflight checks

```bash
for cmd in \
  git make gcc gpg curl tar xz \
  parted losetup mkfs.vfat mount umount mountpoint; do
  command -v "$cmd" >/dev/null || echo "missing: $cmd"
done
```

If any command is missing, install the corresponding package first.

## PATH gotcha for admin tools

On some minimal Debian shells, `parted` and similar tools are under
`/usr/sbin` and are not present in the default non-root `PATH`.

Use this before running image assembly scripts:

```bash
export PATH="/usr/sbin:/sbin:$PATH"
```

The Thunder Den image assembly helper script also prepends these paths
automatically.
