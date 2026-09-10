# Thunder Den

**Turn a spare laptop into an airgapped Bitcoin signer.**

Thunder Den is being redesigned to boot from USB and run in memory. Import your
existing recovery words, review transactions on the laptop, and exchange data
through QR codes.

It is built to be understandable. Even if you do not write code, you can use an
AI assistant to explore the source, examine the claims, and guide you through
building the published image yourself. The project is designed to make
independent, AI-assisted review practical.

Inspired by SeedSigner's stateless approach and descriptor-based hardware-wallet
designs, including Ledger and BitBox02. Bitcoin Core provides the Bitcoin
functionality at its heart.

**Status: v2 is under development.** A bootable v2 release is not available yet.
Use test networks only; this project is not ready to protect mainnet funds.

## Claims we are building toward

- Your seed and private keys stay in RAM.
- Thunder Den saves no wallet state between boots.
- Thunder Den does not write to the boot USB or the laptop's disks.
- The running signer cannot access disk storage, Ethernet, Wi-Fi, or Bluetooth.
- Wallet data and transactions move through QR codes.
- You review and approve transactions on the laptop before signing.
- You can rebuild the released USB image and compare its SHA-256.

## Planned features

- Import English BIP39 recovery words and an optional ASCII passphrase once per boot.
- Descriptor-based wallets, including SegWit and Taproot Miniscript.
- BIP-388 wallet policies with seed-bound registration proofs.
- BIP44, BIP49, BIP84, and BIP86 defaults without prior registration.
- Partial signing when other signatures or spending conditions are still needed.
- One USB image for x86-64 laptops with legacy BIOS or UEFI.
- A Docker build for Linux, macOS, and Windows.

## What you trust

Thunder Den relies on your hardware and firmware, your build computer, and pinned
upstream software, including Bitcoin Core and Linux. The project audit covers
Thunder Den's code, configuration, and use of those dependencies, not a complete
re-audit of upstream projects. AI-assisted review helps you examine that work;
it is not a security certification.

Firmware behavior and physical attacks on memory are outside the software's
guarantees. RAM-only operation means no persistent wallet storage, not a promise
that every trace in physical memory becomes unrecoverable at power-off.

## Follow the work

- [Design](docs/DESIGN.md)
- [Build and tests](docs/BUILD.md)
- [Dependencies](docs/DEPENDENCIES.md)
- [Implementation status](docs/STATUS.md)

Tests and development tools run on the build computer. They are not part of the
production image. Software-wallet integrations and reference clients will follow
the signer protocol; no particular software wallet defines Thunder Den's design.

## Acknowledgments

Thanks to **SeedSigner** for inspiration, **Salvatore Ingala (Ledger)** for the
wallet-policy and registration design, and **Bitcoin Core's contributors** for
the Bitcoin engine. Thunder Den also builds on Linux, Buildroot, and the other
upstream projects listed in its dependency documentation.
