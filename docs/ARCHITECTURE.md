# Thunder Den Architecture

## Mission

Build a USB-boot, offline, stateless Bitcoin signer that is small enough to be
auditable and strict enough to be trusted.

## Security goals

- Never require persistent secret storage.
- Keep root filesystem RAM-backed during runtime.
- Never need outbound network access for signing.
- Never mount host machine disks.
- Keep dependency graph small, explicit, and pinned.
- Make builds reproducible enough for independent verification.

## Non-goals for v1

- General-purpose wallet UX.
- Multi-account management.
- Animated multi-part QR standards (UR/BBQR) in first release.
- x86 + Apple Silicon support in same first artifact (x86 first).

## High-level flow (BIP84 / PSBT)

1. User boots Thunder Den image.
2. Text menu starts automatically (no login).
3. User selects signing network context (mainnet/testnet).
4. User enters English BIP39 mnemonic (and optional passphrase).
5. Tool derives BIP84 account key (`m/84h/0h/0h` for mainnet, `m/84h/1h/0h` for testnet) using `bitcoin-bash-tools`.
6. Tool builds descriptors:
   - `wpkh(<account_xprv>/0/*)`
   - `wpkh(<account_xprv>/1/*)`
7. Tool calls Bitcoin Core `descriptorprocesspsbt` offline.
8. Signed PSBT is shown as text and QR.
9. Reboot wipes runtime state.

## Dependency policy

Allowed runtime dependencies are intentionally narrow:

- Linux kernel + BusyBox init/userspace
- Bash (required by `bitcoin-bash-tools`)
- Bitcoin Core (`bitcoind`, `bitcoin-cli`)
- OpenSSL + `dc` (required by `bitcoin-bash-tools`)
- `qrencode` (display)
- `zbar` (scan)

Anything else should be treated as an exception and justified in review.

## USB / radio policy (selected default)

v1 default:

- Keep USB keyboard and USB camera usable.
- Compile out USB storage support (`CONFIG_USB_STORAGE=n`, `CONFIG_UAS=n`).
- Disable Bluetooth and Wi-Fi stacks.
- No automount daemon and no host disk mounts.

## Kernel-level exfiltration reduction

Common concern: "Can the signer leak seed data through radios or storage that
I accidentally leave connected?"

Thunder Den tries to reduce those paths directly in kernel configuration:

- No Wi-Fi/Bluetooth stack (`WIRELESS`, `WLAN`, `BT`, `CFG80211`, `MAC80211` disabled).
- No wired NIC driver families (`ETHERNET`, `VIRTIO_NET`, `E1000*`, `R8169`, etc. disabled).
- USB mass-storage disabled (`USB_STORAGE`, `UAS` disabled) while keyboard/camera remain enabled.
- Block-storage stack for host disks disabled (`SCSI`, `ATA`, `VIRTIO_BLK`, `BLK_DEV_SD` disabled).
- Runtime disk filesystems and automount paths disabled (`EXT4_FS`, `AUTOFS4_FS` disabled).
- Kernel modules disabled (`MODULES=n`) so these surfaces cannot be re-enabled at runtime.

What remains enabled is the minimum Linux runtime plumbing (for example
`proc`, `sysfs`, `tmpfs`) needed for userspace and guard checks.

This is an attack-surface reduction layer, not an absolute guarantee against
all hardware/firmware compromise scenarios.

The source of truth for exact kernel toggles is
`buildroot-external/board/thunderden/linux.config`.

## Entropy policy (selected default)

- Kernel command line sets `random.trust_cpu=off random.trust_bootloader=off`.
- Keep hardware RNG support enabled (`HW_RANDOM_*`, including `HW_RANDOM_VIRTIO`).
- Keep `virtio-rng` for VM test coverage; real hardware does not depend on virtio.
- Any future seed/mnemonic generation flow must run `thunderden_entropy_guard.sh` first.
- Entropy guard uses a strict infinite wait (no timeout) until kernel CSPRNG is initialized.

## Runtime storage model

- Kernel embeds initramfs rootfs (`BR2_TARGET_ROOTFS_INITRAMFS`).
- Boot image only provides EFI partition and kernel payload.
- Boot runtime guard enforces RAM-backed root, tmpfs `/tmp` + `/run`, and swap-off before TUI launch.

## Why `descriptorprocesspsbt`

`descriptorprocesspsbt` can sign from descriptors directly and does not require
wallet state persistence. That aligns with stateless operation and allows using
BIP39-derived descriptor keys from `bitcoin-bash-tools`.

## TUI options evaluated

- `gum`: excellent shell UX tool, but adds a non-essential binary dependency.
- `huh`: clean forms API, but requires a compiled Go app and larger dependency surface.
- `bubbles` / `bubbletea`: very capable for complex TUIs, but overkill for v1 signing menu.
- Plain Bash TUI: smallest trusted surface for first release, chosen for v1.

## Build trust model

- Pin exact versions and commits.
- Verify source signatures and checksums.
- Keep local patchset small and readable.
- Publish build manifest and image hashes.
- Rebuild from clean VM and compare resulting artifacts.

## Current implementation scaffolding

- Buildroot external tree: `buildroot-external/`.
- Defconfig entrypoint: `buildroot-external/configs/thunderden_x86_64_defconfig`.
- Post-build integration: `buildroot-external/board/thunderden/post-build.sh`.
- Post-image integration: `buildroot-external/board/thunderden/post-image.sh`.
- Repro helpers: `scripts/build_thunderden.sh`, `scripts/fetch_bitcoin_bash_tools.sh`.
