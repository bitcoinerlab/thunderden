# Testing Environment (QEMU First)

Use this flow before touching real hardware.

## Goal

Validate the signer pipeline end-to-end:

- BIP39 mnemonic input
- BIP84 descriptor derivation
- Bitcoin Core offline `descriptorprocesspsbt`
- Signed PSBT output (text + QR)

## 1) Host prerequisites

Install tools on a Linux test host:

```bash
sudo apt update
sudo apt install -y bitcoind bitcoin-cli qrencode zbar-tools bash coreutils openssl dc
```

Clone and source `bitcoin-bash-tools`:

```bash
git clone https://github.com/grondilu/bitcoin-bash-tools.git
export THUNDERDEN_BBT_SH="$PWD/bitcoin-bash-tools/bitcoin.sh"
```

## 2) Smoke test the signer script

Use a known mnemonic (test only):

```bash
MNEMONIC="abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
```

Provide an unsigned PSBT (base64) from your coordinator and run:

```bash
printf '%s\n' "$MNEMONIC" | \
  ./scripts/thunderden_sign_psbt.sh \
  --mnemonic-stdin \
  --psbt "cHNidP..."
```

Expected:

- stderr prints `complete=true` or `complete=false`
- stdout prints updated/signed PSBT base64

## 3) Run the TUI locally

```bash
chmod 0755 ./scripts/thunderden_*.sh
THUNDERDEN_SIGNER="$PWD/scripts/thunderden_sign_psbt.sh" \
THUNDERDEN_SCANNER="$PWD/scripts/thunderden_scan_qr.sh" \
THUNDERDEN_SHOW_QR="$PWD/scripts/thunderden_show_qr.sh" \
./scripts/thunderden_tui.sh
```

For camera-less testing, choose manual PSBT paste mode.

## 4) PSBT test vectors (recommended)

Create deterministic vectors and keep them in a private test folder:

- mnemonic
- expected first receive addresses for `m/84h/0h/0h/0/i`
- unsigned PSBT sample
- expected signed PSBT hash

This lets you detect regressions immediately.

## 5) QEMU image tests (after Buildroot image exists)

First create the UEFI disk image:

```bash
./out/buildroot/images/make-uefi-image.sh \
  --binaries-dir ./out/buildroot/images \
  --output thunderden-uefi.img
```

Boot with network disabled:

```bash
cp /usr/share/OVMF/OVMF_VARS.fd ./OVMF_VARS.fd

qemu-system-x86_64 \
  -machine q35,accel=kvm \
  -m 2048 \
  -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=./OVMF_VARS.fd \
  -drive if=virtio,format=raw,file=thunderden-uefi.img \
  -nic none \
  -serial mon:stdio
```

Validate inside guest:

- TUI starts automatically.
- no active network path is used by signer flow (`cat /proc/net/dev`, `rfkill list` if available).
- `mount` shows no host disk mounts.
- Signing works and output QR renders.
- Reboot clears prior sensitive data.
