# Thunder Den

Thunder Den is an offline Bitcoin signer image (USB-boot) focused on trust,
minimal dependencies, and stateless operation.

## Current v1 direction

- Boot from USB into text mode only.
- No login flow; launch a signer TUI directly.
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

- Default users: download prebuilt `thunderden-uefi.img` and verify signatures/hashes.
- Advanced users: reproducible self-build on Linux host/VM.
- Build host support is Linux-only in this repo flow.

## Accepted default for USB policy

v1 keeps keyboard and camera support, while disabling USB storage and radio
stacks (Wi-Fi/Bluetooth). This keeps the machine usable for QR signing and
typing while reducing attack surface.

## Repo layout

- `docs/ARCHITECTURE.md`: trust model, components, dependency policy.
- `docs/BUILD_IMAGE.md`: detailed human-followable image build guide.
- `docs/DEPENDENCIES.md`: minimal dependency set and pinned references.
- `docs/HOST_SETUP_LINUX.md`: from-scratch Linux host requirements and setup.
- `docs/RELEASE_VERIFICATION.md`: prebuilt image verification and release signing flow.
- `docs/TESTING_QEMU.md`: local testing and validation flow.
- `buildroot-external/`: Buildroot external tree (`thunderden_x86_64_defconfig`).
- `scripts/thunderden_tui.sh`: no-login text menu for signing.
- `scripts/thunderden_sign_psbt.sh`: BIP84 descriptor signing pipeline.
- `scripts/thunderden_runtime_guard.sh`: boot-time runtime policy checks.
- `scripts/thunderden_scan_qr.sh`: camera scanner helper.
- `scripts/thunderden_show_qr.sh`: terminal QR display helper.
- `scripts/build_thunderden.sh`: build helper for Buildroot external tree.
- `scripts/fetch_bitcoin_bash_tools.sh`: pin/fetch helper for `bitcoin-bash-tools`.
- `scripts/test_shell_syntax.sh`: shell syntax checks.
- `experiments/`: quarantined prototype scripts not used in runtime image.

## Fast path (after source verification)

```bash
./scripts/fetch_bitcoin_bash_tools.sh
./scripts/build_thunderden.sh --buildroot-dir /path/to/buildroot
./out/buildroot/images/make-uefi-image.sh --binaries-dir ./out/buildroot/images --output thunderden-uefi.img
```

Then flash `thunderden-uefi.img` to USB and boot.

Default prebuilt-image verification flow is documented in
`docs/RELEASE_VERIFICATION.md`.

You can also use shortcuts:

```bash
make fetch-bbt
make syntax
make build BR_SRC=/path/to/buildroot
```

## Key references

- Bitcoin Bash Tools: <https://github.com/grondilu/bitcoin-bash-tools>
- Bitcoin Core RPC docs: <https://bitcoincore.org/en/doc/>

## Notes

- This project intentionally avoids broad dependency sprawl.
- The build guide pins source versions and includes signature verification steps.
- The first implementation focuses on single-frame base64 PSBT QR (`cHNidP...`).
