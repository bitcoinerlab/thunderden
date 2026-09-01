# Thunder Den Buildroot Board Notes

This board directory wires the Buildroot image to boot directly into Thunder Den.

## What it does

- Installs Thunder Den runtime scripts into `/usr/bin`.
- Runs boot-time runtime guard before starting TUI.
- Installs pinned `bitcoin-bash-tools` into `/opt/bitcoin-bash-tools`.
- Pins Bitcoin Core package source to 31.1 with verified hashes.
- Pins Linux 7.0.14 and uses matching kernel headers.
- Narrows Bitcoin Core build outputs to daemon + CLI only.
- Prunes unused Bitcoin Core helper binaries from target rootfs.
- Uses a custom `linux.config` baseline for Thunder Den runtime and hardening.
- Kernel config disables unneeded sound, external NIC, and USB mass-storage paths.
- Kernel cmdline enforces `random.trust_cpu=off random.trust_bootloader=off`.
- Replaces BusyBox `inittab` to launch `thunderden_boot.sh` on `tty1`.
- Overrides GRUB config for shared BIOS+UEFI boot entry.
- Publishes `thunderden.SHA256SUMS` in `output/images`.

## Inputs expected

- Repo runtime scripts in `scripts/runtime/`.
- `bitcoin-bash-tools` source in `third_party/bitcoin-bash-tools`
  (or override with `THUNDERDEN_BBT_SRC`).

## Output helper

`output/images/make-image.sh` creates the hybrid BIOS+UEFI image. Its `--small`
mode creates the minimum-size FAT16 image for direct x86_64 UEFI-stub boot.

It uses:

- `output/images/efi-part/`
- `output/images/bzImage`
- `output/images/boot.img`
- `output/images/grub.img`

Small mode uses only `output/images/bzImage`. It does not use GRUB and does not
support legacy BIOS.

The helper uses rootless assembly (no loop devices or mounts).

Runtime root filesystem is embedded into the kernel as initramfs.
