# Thunder Den Architecture

## Mission

Build a USB-boot, offline, stateless Bitcoin signer that is small enough to be
auditable and strict enough to be trusted.

## Security goals

- Never require persistent secret storage.
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
3. User enters BIP39 mnemonic (and optional passphrase).
4. Tool derives BIP84 account key (`m/84h/0h/0h`) using `bitcoin-bash-tools`.
5. Tool builds descriptors:
   - `wpkh(<account_xprv>/0/*)`
   - `wpkh(<account_xprv>/1/*)`
6. Tool calls Bitcoin Core `descriptorprocesspsbt` offline.
7. Signed PSBT is shown as text and QR.
8. Reboot wipes runtime state.

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
- Disable USB storage runtime modules (`usb_storage`, `uas`) after boot.
- Disable Bluetooth and Wi-Fi stacks.
- No automount daemon and no host disk mounts.

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
