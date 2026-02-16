# Thunder Den Buildroot Board Notes

This board directory wires the Buildroot image to boot directly into Thunder Den.

## What it does

- Installs Thunder Den runtime scripts into `/usr/bin`.
- Runs boot-time runtime guard before starting TUI.
- Installs pinned `bitcoin-bash-tools` into `/opt/bitcoin-bash-tools`.
- Pins Bitcoin Core package source to 30.2 with verified hashes.
- Narrows Bitcoin Core build outputs to daemon + CLI only.
- Prunes unused Bitcoin Core helper binaries from target rootfs.
- Uses a minimal `linux.config` fragment file for signer-specific kernel deltas.
- Kernel override disables unneeded sound, external NIC, and storage filesystems/drivers.
- Replaces BusyBox `inittab` to launch `thunderden_boot.sh` on `tty1`.
- Blacklists USB storage, Bluetooth, and Wi-Fi modules via `/etc/modprobe.d/`.
- Overrides GRUB config for shared BIOS+UEFI boot entry.
- Publishes `thunderden.SHA256SUMS` in `output/images`.

## Inputs expected

- Repo runtime scripts in `../scripts`.
- `bitcoin-bash-tools` source in `../third_party/bitcoin-bash-tools`
  (or override with `THUNDERDEN_BBT_SRC`).

## Output helper

`output/images/make-image.sh` creates:

- max-compat hybrid BIOS+UEFI image (default mode)
- tiny UEFI-only image (`--small`)

Default mode uses:

- `output/images/efi-part/`
- `output/images/bzImage`
- `output/images/boot.img`
- `output/images/grub.img`

The helper uses rootless assembly (no loop devices or mounts).

Runtime root filesystem is embedded into the kernel as initramfs.
