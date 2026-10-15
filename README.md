# Thunder Den

**Turn a spare laptop into an airgapped Bitcoin signer.**

Thunder Den's image boots from USB and runs in memory. Generate new recovery words
or import your existing ones, review transactions on the laptop and exchange data
through QR codes.

Thunder Den is designed so you can use AI to check its claims against the code
before you use it. Its own signer and worker code, build scripts, boot settings
and image-content/assembly checks total about 5,800 lines, including comments and
blank lines. We keep that code clear, with few dependencies and a simple build process.

Inspired by SeedSigner's stateless approach and descriptor-based hardware-wallet
designs, including Ledger, BitBox02 and Jade. Bitcoin Core provides key
derivation, descriptor handling and transaction signing.

**Status: Under active development. Use at your own risk.** The signer is usable
with [Sparrow](#use-with-sparrow) and [our Liana fork](#use-with-liana).
See [implementation status](docs/STATUS.md) for completed checks and remaining work.

## Download and boot

**[Download the USB image (64 MiB)](https://github.com/bitcoinerlab/thunderden/releases/download/v0.0.1-preview.8/thunderden.img)**
· [SHA-256 checksum](https://github.com/bitcoinerlab/thunderden/releases/download/v0.0.1-preview.8/thunderden.img.sha256)
· [Preview release notes and file inventory](https://github.com/bitcoinerlab/thunderden/releases/tag/v0.0.1-preview.8)

This image boots x86-64 (Intel/AMD) laptops with BIOS or UEFI. You can prepare the
USB on macOS, Linux or Windows; Apple Silicon Macs can write it but cannot boot it.

1. Download the image and checksum, then [verify the download](docs/BUILD.md#verify-the-image).
2. Open [balenaEtcher](https://etcher.balena.io/): **Flash from file →
   `thunderden.img` → Select target → your USB drive → Flash**.
3. Wait for verification, eject the drive and boot the laptop from USB. Disable
   Secure Boot to boot this unsigned image.

**Flashing erases the selected USB drive.** See the
[build and USB guide](docs/BUILD.md) for more detail or to build your own image.

## Design claims

- Your seed and private keys stay in RAM.
- Thunder Den saves no wallet state between boots.
- Thunder Den does not write to the boot USB or the laptop's disks.
- The running signer cannot access disk storage, Ethernet, Wi-Fi or Bluetooth.
- Wallet data and transactions move through QR codes.
- You review and approve transactions on the laptop before signing.
- Wallet requests arrive through QR codes; recovery input and approval use the
  laptop's keyboard. Inputs are treated as untrusted and validated.
- The scanner runs separately from the keys and approval controls. Linux restricts
  its file and process access, and the running programs cannot overwrite the
  installed application files. See [scanner isolation](docs/ISOLATION.md).
- You can build the USB image from pinned sources. Recorded clean-build comparisons
  produced matching SHA-256 hashes; see [build verification](docs/STATUS.md#verified-image-behavior).

## Features

- English BIP39 recovery words with an optional printable ASCII passphrase.
- [Generate 12 or 24 recovery words](docs/ENTROPY.md) using Bitcoin Core randomness,
  with optional camera, coin or dice input and full backup re-entry.
- Descriptor-based wallets, including SegWit and Taproot Miniscript.
- BIP-388 wallet policies with seed-bound registration proofs.
- BIP44, BIP49, BIP84 and BIP86 defaults without prior registration.
- Verify scanned addresses against a standard account or loaded multisig wallet.
- Partial signing when other signatures or spending conditions are still needed.
- UR v2 scanning and animated QR output, with the scanner isolated from keys
  and approval.
- One USB image for x86-64 laptops with legacy BIOS or UEFI.
- A Docker/Compose build using pinned Linux containers with native build tools.

## Use with Sparrow

[Sparrow](https://sparrowwallet.com/) exchanges QR codes directly with Thunder Den
for single-signature and sorted-multisig wallets. Use **Sparrow 2.5.5 or later** and
choose **Airgapped Hardware Wallet → Krux** for Thunder Den's keystore.

Share the public key, create the wallet in Sparrow, then scan its transaction QR
on Thunder Den. Multisig setup can be approved directly from a complete PSBT.
The preview 5 image was user-tested with Sparrow multisig on a real device. See the
[Sparrow guide](docs/SPARROW.md) for the steps and validation scope.

## Use with Keeper

**Bitcoin Keeper:** see the [compatibility guide](docs/KEEPER.md) for supported
workflows and current limitations.

## Use with Liana

Thunder Den is currently usable with [our Liana fork](https://github.com/bitcoinerlab/wizardsardine-liana).
The integration has **not yet been submitted to upstream Liana**.

Build and run the GUI using the [pinned installation guide](docs/LIANA.md), which
identifies the Liana, async-hwi and bridge versions and preserves their lockfiles.
Choose **Bitcoin Core or Electrum** as the wallet backend. Thunder Den support for
Liana Connect is disabled until its server supports Thunder Den registration tokens.

On the same computer as Liana, start the [QR bridge](https://github.com/bitcoinerlab/thunderden-qr-bridge)
with Node.js 22 or later:

```sh
npx @bitcoinerlab/thunderden-qr-bridge@0.1.2
```

Choose the same Bitcoin network in Thunder Den and Liana, then open Liana's
hardware-wallet selection and follow the instructions on the bridge's browser page.

## What you trust

Thunder Den relies on your hardware and firmware, your build computer and pinned
dependencies. We use widely scrutinized projects such as Bitcoin Core and Linux,
and disable Linux features the signer does not need. Reviewing Thunder Den's code
still leaves those dependencies and your hardware to review or trust.

You can ask an AI coding assistant to inspect this repository for bugs,
vulnerabilities or backdoors. Start with the
[source-reading map](docs/DESIGN.md#build-and-independent-review) and
[dependency inventory](docs/DEPENDENCIES.md). Check its findings against the code
and tests; AI review alone is not proof of security. Report findings in an issue
or submit a focused pull request.

### Limits

Firmware behavior and physical attacks on memory are outside the software's
guarantees. RAM-only operation means no persistent wallet storage, not a promise
that every trace in physical memory becomes unrecoverable at power-off.

To build your own USB image, follow the [build and test instructions](docs/BUILD.md).

## Follow the work

- [Design](docs/DESIGN.md)
- [Fee verification and compact PSBTs](docs/FEES.md)
- [Scanner isolation](docs/ISOLATION.md)
- [Build and tests](docs/BUILD.md)
- [End-to-end signing walkthrough](docs/WALKTHROUGH.md)
- [Thunder Den QR protocol](docs/PROTOCOL.md)
- [Dependencies](docs/DEPENDENCIES.md)
- [Implementation status](docs/STATUS.md)
- [Audit reports and finding status](audits/README.md)

Tests and development tools run on the build computer. They are not part of the
production image. Wallet applications can use the standard PSBT QR format or
implement Thunder Den's QR commands. No particular software wallet defines
Thunder Den's design.

## Contributing

Contributions are welcome: bug fixes, review findings, documentation and build or
hardware test reports are especially useful. **Simplicity and auditability take
priority over feature count.** Changes must justify their complexity, dependencies
and attack surface; security additions and refactors need a concrete rationale.
See the short [contribution guide](CONTRIBUTING.md) before opening a pull request.

## Acknowledgments

Thanks to **SeedSigner** for inspiration, **Salvatore Ingala (Ledger)** for the
wallet-policy and registration design and **Bitcoin Core's contributors** for
the Bitcoin engine. Thunder Den also builds on Linux, Buildroot and the other
upstream projects listed in its dependency documentation.
