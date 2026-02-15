# Thunder Den Image Build Guide (x86_64 UEFI)

This guide is the canonical Linux-only self-build flow for Thunder Den v1.

## 1) Prepare a clean Linux build host

Use a dedicated Debian VM and follow `docs/HOST_SETUP_LINUX.md` first.

Recommended baseline:

- Debian 12 or Debian 13
- non-root build user with `sudo`
- no unrelated tooling in the same workspace

## 2) Verify Buildroot source (pinned)

```bash
export BR_VER="2025.02.10"
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
cd "$HOME/thunderden_new"
./scripts/fetch_bitcoin_bash_tools.sh
make syntax
```

## 4) Build Thunder Den

```bash
cd "$HOME/thunderden_new"
./scripts/build_thunderden.sh --buildroot-dir "$BR_SRC"
```

Artifacts land in:

- `out/buildroot/images/rootfs.cpio`
- `out/buildroot/images/bzImage`
- `out/buildroot/images/efi-part/`
- `out/buildroot/images/thunderden.SHA256SUMS`
- `out/buildroot/images/make-uefi-image.sh`

## 5) Assemble bootable UEFI disk image

```bash
cd "$HOME/thunderden_new"
./out/buildroot/images/make-uefi-image.sh \
  --binaries-dir ./out/buildroot/images \
  --output thunderden-uefi.img
```

The generated image includes only an EFI partition and boots a kernel with
embedded initramfs rootfs (RAM-backed runtime root).

Generate hash for release/testing:

```bash
sha256sum thunderden-uefi.img > thunderden-uefi.img.sha256
sha256sum -c thunderden-uefi.img.sha256
```

## 6) Flash USB image

```bash
sudo dd if=thunderden-uefi.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

Replace `/dev/sdX` with the real USB device.

## 7) First boot validation checklist

- boots directly to Thunder Den TUI
- no login prompt
- signing flow works from QR/paste input
- Wi-Fi/Bluetooth disabled
- USB keyboard/camera usable
- reboot clears runtime secrets

## 8) Release bundle checklist

- `thunderden-uefi.img`
- `thunderden-uefi.img.sha256`
- `SHA256SUMS` + `SHA256SUMS.asc`
- Buildroot version and verification logs
- Thunder Den repo commit hash
- pinned `bitcoin-bash-tools` commit

See `docs/RELEASE_VERIFICATION.md` for end-user verification flow.
