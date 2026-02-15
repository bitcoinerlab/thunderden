# Thunder Den Buildroot Board Notes

This board directory wires the Buildroot image to boot directly into Thunder Den.

## What it does

- Installs Thunder Den runtime scripts into `/usr/bin`.
- Runs boot-time runtime guard before starting TUI.
- Installs pinned `bitcoin-bash-tools` into `/opt/bitcoin-bash-tools`.
- Replaces BusyBox `inittab` to launch `thunderden_boot.sh` on `tty1`.
- Blacklists Bluetooth/Wi-Fi modules via `/etc/modprobe.d/`.
- Overrides GRUB config for BIOS and EFI outputs.
- Publishes `thunderden.SHA256SUMS` in `output/images`.

## Inputs expected

- Repo runtime scripts in `../scripts`.
- `bitcoin-bash-tools` source in `../third_party/bitcoin-bash-tools`
  (or override with `THUNDERDEN_BBT_SRC`).

## Output helper

`output/images/make-uefi-image.sh` creates a flashable UEFI disk image from:

- `output/images/efi-part/`
- `output/images/bzImage`

Runtime root filesystem is embedded into the kernel as initramfs.
