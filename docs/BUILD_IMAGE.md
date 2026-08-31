# Thunder Den Image Build Guide (x86_64 Hybrid BIOS+UEFI)

This guide is the canonical Linux-only self-build flow for Thunder Den v1.

## 1) Prepare a clean Linux build host

Use a dedicated Debian VM and follow `docs/HOST_SETUP_LINUX.md` first.

Recommended baseline:

- Debian 12 or Debian 13
- non-root build user with `sudo`
- no unrelated tooling in the same workspace

## 2) Verify Buildroot source (pinned)

```bash
export BR_VER="2025.11.1"
mkdir -p "$HOME/thunderden-build/src"
cd "$HOME/thunderden-build/src"

curl -LO "https://buildroot.org/downloads/buildroot-${BR_VER}.tar.xz"
curl -LO "https://buildroot.org/downloads/buildroot-${BR_VER}.tar.xz.sign"
```

Buildroot `*.sign` files are clearsigned messages that include hashes and the
key URL. Import key and verify the signed message:

```bash
KEY_URL="$(awk '/^from https:/{print $2}' "buildroot-${BR_VER}.tar.xz.sign")"
curl -fsSL "$KEY_URL" -o buildroot-release-key.asc
gpg --import buildroot-release-key.asc
gpg --verify "buildroot-${BR_VER}.tar.xz.sign"
```

Then verify the tarball hash against the signed message:

```bash
EXPECTED_SHA256="$(awk '/^SHA256:/ {print $2}' "buildroot-${BR_VER}.tar.xz.sign")"
echo "${EXPECTED_SHA256}  buildroot-${BR_VER}.tar.xz" | sha256sum -c -
```

Extract source tree:

```bash
tar -xf "buildroot-${BR_VER}.tar.xz"
export BR_SRC="$HOME/thunderden-build/src/buildroot-${BR_VER}"
```

## 3) Prepare Thunder Den repo

```bash
cd "$HOME/thunderden"
./scripts/build/fetch_bitcoin_bash_tools.sh
./scripts/build/test_shell_syntax.sh
```

## 4) Build Thunder Den

```bash
cd "$HOME/thunderden"
./scripts/build/build_thunderden.sh --buildroot-dir "$BR_SRC"
```

Artifacts land in:

- `out/buildroot/images/rootfs.cpio`
- `out/buildroot/images/bzImage`
- `out/buildroot/images/efi-part/`
- `out/buildroot/images/thunderden.SHA256SUMS`
- `out/buildroot/images/make-image.sh`

## 5) Assemble bootable hybrid BIOS+UEFI disk image

```bash
cd "$HOME/thunderden"

# Max compatibility image (BIOS+UEFI, FAT32, fixed 64 MiB)
./out/buildroot/images/make-image.sh \
  --binaries-dir ./out/buildroot/images \
  --output thunderden.img

# Tiny image (smallest UEFI-only FAT16 size that fits current payload)
./out/buildroot/images/make-image.sh \
  --binaries-dir ./out/buildroot/images \
  --output thunderden-small.img \
  --small
```

This image assembly step is rootless (no `sudo`, no loop mounts).

Both images boot a kernel with embedded initramfs rootfs (RAM-backed runtime
root).

Use `thunderden.img` as the default release artifact. `thunderden-small.img`
is a best-effort tiny UEFI-only variant and may be less firmware-compatible.

Generate hash for release/testing:

```bash
sha256sum thunderden.img > thunderden.img.sha256
sha256sum -c thunderden.img.sha256
sha256sum thunderden-small.img > thunderden-small.img.sha256
sha256sum -c thunderden-small.img.sha256
```

## 6) Flash USB image

```bash
sudo dd if=thunderden.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

Replace `/dev/sdX` with the real USB device.

## 7) First boot validation checklist

- boots directly to Thunder Den TUI
- no login prompt
- signing flow works from QR/paste input
- Wi-Fi/Bluetooth disabled
- USB keyboard/camera usable
- `/proc/cmdline` includes `random.trust_cpu=off` and `random.trust_bootloader=off`
- reboot clears runtime secrets

## 8) Release bundle checklist

- `thunderden.img`
- `thunderden.img.sha256`
- `thunderden-small.img` (UEFI-only tiny variant)
- `thunderden-small.img.sha256`
- `SHA256SUMS` + `SHA256SUMS.asc`
- Buildroot version and verification logs
- Thunder Den repo commit hash
- pinned `bitcoin-bash-tools` commit

See `docs/RELEASE_VERIFICATION.md` for end-user verification flow.
