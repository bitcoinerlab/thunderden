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
- x86 + Apple Silicon support in same first artifact (x86 first).

## Current MVP flow (BIP84 / PSBT)

1. User boots Thunder Den image.
2. Text menu starts automatically (no login).
3. User selects signing network context (mainnet/testnet).
4. User scans an unsigned PSBT with the camera.
5. User enters an English BIP39 mnemonic and optional passphrase.
6. Tool derives BIP84 account key (`m/84h/0h/0h` for mainnet, `m/84h/1h/0h` for testnet) using `bitcoin-bash-tools`.
7. Tool builds descriptors:
   - `wpkh(<account_xprv>/0/*)`
   - `wpkh(<account_xprv>/1/*)`
8. Tool calls Bitcoin Core `descriptorprocesspsbt` offline.
9. Signed PSBT is shown as text and QR.
10. Reboot wipes runtime state.

The MVP derives a fixed BIP84 account internally. It does not yet accept a
wallet policy, register custom accounts, support multisig, or review all
transaction details before signing.

## Target wallet-policy architecture

The final architecture remains descriptor-based and stateless, but represents
each account as a BIP-388 wallet policy supplied by the coordinator.

- Use Ledger's version 2 wallet-policy serialization and wallet ID exactly.
- Allow Ledger's BIP44, BIP49, BIP84, and BIP86 default accounts without registration when every standard-path and xpub check passes.
- Require registration for multisig, Miniscript, unusual paths, named accounts, and every other custom policy.
- Return a seed-derived HMAC as proof that the user previously approved the exact named policy.
- Use a Thunder Den SLIP-0021 label so registration HMACs are not interchangeable with Ledger HMACs.
- Require the coordinator to store and provide the complete policy and HMAC on every signing request.
- Keep `REGISTER_WALLET` and `SIGN_PSBT` as separate airgap commands.
- Use Bitcoin Core to validate materialized descriptors, classify policy-owned scripts, analyze PSBTs, and sign.
- Return a complete PSBT for single-signature wallets or an updated partial PSBT for multisig.

The exact policy serialization, HMAC construction, default-wallet rules, and
request flows are defined in `docs/WALLET_POLICIES.md`.

## Dependency policy

Allowed runtime dependencies are intentionally narrow:

- Linux kernel + BusyBox init/userspace
- Bash (required by `bitcoin-bash-tools`)
- Bitcoin Core (`bitcoind`, `bitcoin-cli`)
- OpenSSL + `dc` (required by `bitcoin-bash-tools`)
- `jq` (target architecture: safe extraction of Bitcoin Core JSON)
- `qrencode` (display)
- Custom scanner with `zbar` and `libv4l`

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
- Thunder Den imports a mnemonic. It does not generate one.

## Runtime storage model

- Kernel embeds initramfs rootfs (`BR2_TARGET_ROOTFS_INITRAMFS`).
- Boot image only provides EFI partition and kernel payload.
- Boot runtime guard enforces RAM-backed root, tmpfs `/tmp` + `/run`, and swap-off before TUI launch.

## Why `descriptorprocesspsbt`

`descriptorprocesspsbt` can sign from descriptors directly and does not require
wallet state persistence. That aligns with stateless operation and allows using
BIP39-derived descriptor keys from `bitcoin-bash-tools`. In the target
architecture, Thunder Den materializes the supplied BIP-388 policy as Core
descriptors and inserts private material only for keys proven to belong to the
entered seed.

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

## Source layout

- Buildroot external tree: `buildroot-external/`.
- Defconfig entrypoint: `buildroot-external/configs/thunderden_x86_64_defconfig`.
- Post-build integration: `buildroot-external/board/thunderden/post-build.sh`.
- Post-image integration: `buildroot-external/board/thunderden/post-image.sh`.
- Build scripts: `scripts/build/`.
- Files installed in the image: `scripts/runtime/`.
- Unfinished code not installed in the image: `scripts/todo/`.
