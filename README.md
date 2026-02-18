# Thunder Den

Thunder Den is an offline Bitcoin signer image (USB-boot) focused on trust,
minimal dependencies, and stateless operation.

## Current v1 direction

- Boot from USB into text mode only.
- No login flow; launch a signer TUI directly.
- Ship one hybrid BIOS+UEFI USB image for broad hardware compatibility.
- Stateless runtime: initramfs root in RAM, `/tmp` and `/run` on tmpfs, reboot clears all secrets.
- Network disabled for the signing workflow.
- TUI includes network selection for signing context (mainnet/testnet).
- Use Bitcoin Core for PSBT signing through `descriptorprocesspsbt`.
- Use `bitcoin-bash-tools` only for BIP39 (`mnemonic -> seed -> BIP84 descriptors`).
- QR-only transport (scan unsigned PSBT, display signed PSBT).
- TUI flow is camera-first for unsigned PSBT input.

System runtime guardrails:

- Boot guard checks runtime policy before launching the TUI (`/` RAM-backed, `/tmp` + `/run` tmpfs, swap off).
- On guard failure, Thunder Den stays on an error screen and waits for user action.

## Distribution paths

- Default users: download prebuilt `thunderden.img` (hybrid BIOS+UEFI) and verify signatures/hashes.
- Advanced users: reproducible self-build on Linux host/VM.
- Build host support is Linux-only in this repo flow.

## Accepted default for USB policy

v1 keeps keyboard and camera support, while disabling USB storage and radio
stacks (Wi-Fi/Bluetooth). This keeps the machine usable for QR signing and
typing while reducing attack surface.

## Kernel exfiltration controls (plain language)

People often worry about a signer leaking a seed through radios or by copying
data to attached storage. Thunder Den reduces those paths at kernel level:

- No Wi-Fi or Bluetooth stack is compiled in.
- No wired Ethernet driver family is compiled in.
- USB keyboard/camera support is kept, but USB mass-storage support is compiled out.
- Host-disk storage paths are compiled out (SCSI/ATA/virtio block stack disabled).
- Host-disk filesystems are compiled out for runtime policy (`ext4` and automount path disabled).
- Kernel modules are disabled, so features cannot be re-enabled later by loading modules.
- Root runtime stays RAM-backed (initramfs + tmpfs), so normal operation does not mount host disks.

Some virtual filesystems (for example `proc`, `sysfs`, `tmpfs`) remain enabled
because Linux userspace needs them. This is expected and does not re-enable
host-disk access by itself.

These controls reduce exfiltration surface at kernel level, but they are not a
replacement for firmware, hardware, and supply-chain trust checks.

For exact switches, see `buildroot-external/board/thunderden/linux.config`.

## Entropy policy (default)

- Kernel boot args explicitly set `random.trust_cpu=off random.trust_bootloader=off`.
- Hardware RNG paths remain enabled (including `virtio-rng` for VM testing).
- Future seed/mnemonic generation flows must run `thunderden_entropy_guard.sh` first; it waits indefinitely for kernel RNG readiness.

## Repo layout

- `docs/ARCHITECTURE.md`: trust model, components, dependency policy.
- `docs/BUILD_IMAGE.md`: detailed human-followable image build guide.
- `docs/BUILD_DOCKER.md`: Docker-based build flow for macOS/Linux/Windows (WSL2).
- `docs/DEPENDENCIES.md`: minimal dependency set and pinned references.
- `docs/HOST_SETUP_LINUX.md`: from-scratch Linux host requirements and setup.
- `docs/RELEASE_VERIFICATION.md`: prebuilt image verification and release signing flow.
- `buildroot-external/`: Buildroot external tree (`thunderden_x86_64_defconfig`).
- `scripts/thunderden_tui.sh`: no-login text menu for signing.
- `scripts/thunderden_sign_psbt.sh`: BIP84 descriptor signing pipeline.
- `scripts/thunderden_export_bip84_descriptor.sh`: exports single BIP84 import descriptor (`xpub/tpub`) as text + QR.
- `scripts/thunderden_runtime_guard.sh`: boot-time runtime policy checks.
- `scripts/thunderden_entropy_guard.sh`: strict entropy readiness gate for seed generation flows.
- `scripts/thunderden_scan_qr.sh`: camera scanner helper.
- `scripts/thunderden_show_qr.sh`: terminal QR display helper (auto static/animated with live zoom+density keys).
- `scripts/build_thunderden.sh`: build helper for Buildroot external tree.
- `scripts/docker_build_thunderden.sh`: Docker build helper without bind-mount compile.
- `scripts/fetch_bitcoin_bash_tools.sh`: pin/fetch helper for `bitcoin-bash-tools`.
- `scripts/test_shell_syntax.sh`: shell syntax checks.
- `test-vectors/qr-signing/`: deterministic QR + PSBT vectors for webcam signing tests.
- `experiments/`: quarantined prototype scripts not used in runtime image.

## Fast path (after source verification)

```bash
./scripts/fetch_bitcoin_bash_tools.sh
./scripts/build_thunderden.sh --buildroot-dir /path/to/buildroot
./out/buildroot/images/make-image.sh --binaries-dir ./out/buildroot/images --output thunderden.img
```

The hybrid image assembly helper is rootless (no loop-mount step).

Even though the actual boot payload is small (roughly low-20s MiB today), the
default output image size is 64 MiB. This keeps a FAT32 boot partition with
enough headroom for old firmware quirks and supports one image that boots on
both legacy BIOS and UEFI machines.

Docker builds generate two artifacts by default:

- `thunderden.img`: max-compat BIOS+UEFI image (FAT32, 64 MiB)
- `thunderden-small.img`: smallest current payload fit (UEFI-only, FAT16, best-effort compatibility)

Then flash `thunderden.img` to USB and boot.

Default prebuilt-image verification flow is documented in
`docs/RELEASE_VERIFICATION.md`.

## Key references

- Bitcoin Bash Tools: <https://github.com/grondilu/bitcoin-bash-tools>
- Bitcoin Core RPC docs: <https://bitcoincore.org/en/doc/>

## Notes

- This project intentionally avoids broad dependency sprawl.
- The build guide pins source versions and includes signature verification steps.
- Scanner support includes direct and multipart PSBT QR payloads (base64, UR, BBQR, `pMofN`, hex, base43), normalized to base64 for signing.
