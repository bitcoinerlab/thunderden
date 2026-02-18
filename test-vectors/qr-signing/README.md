# QR Signing Test Vectors

Deterministic PSBT vectors for webcam-based Thunder Den signing tests.

Each vector includes:

- `unsigned.psbt.txt`: base64 PSBT payload (`cHNidP...`).
- `signed.psbt.txt`: expected signed PSBT output from Thunder Den signing flow.
- `unsigned.psbt.qr.png`: single-frame QR image for camera scan tests.
- `metadata.json`: mnemonic, passphrase, paths, amounts, and expected unsigned txid.

All vectors use fake previous outputs, so they are not broadcastable transactions.

## Important safety note

Mnemonics in this directory are public test mnemonics.
Never use them for storing real bitcoin.

## Quick manual test flow

1. Boot Thunder Den.
2. Select the vector network (mainnet or testnet).
3. Show the vector `unsigned.psbt.qr.png` on a second screen.
4. Scan QR in Thunder Den.
5. Enter the vector mnemonic and passphrase from `metadata.json`.
6. Confirm signing succeeds and compare the shown signed PSBT against `signed.psbt.txt`.

## Optional local verification

You can verify the signed PSBT output matches vector expectations:

```bash
python3 test-vectors/qr-signing/verify_signed_psbt.py \
  --vector tv1-testnet-basic \
  --signed-psbt-file test-vectors/qr-signing/vectors/tv1-testnet-basic/signed.psbt.txt
```

## Regeneration

Regenerate all vector files (requires `python3`, `embit`, and `qrencode`):

```bash
python3 test-vectors/qr-signing/generate_vectors.py
```
