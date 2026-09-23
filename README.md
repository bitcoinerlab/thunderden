# Thunder Den

**Turn a spare laptop into an airgapped Bitcoin signer.**

Thunder Den's development image boots from USB and runs in memory. Import your
existing recovery words, review transactions on the laptop and exchange data
through QR codes.

Thunder Den is designed to be easy to audit with AI before you use it. We aim for
small, clear code, few dependencies and a simple build process.

Inspired by SeedSigner's stateless approach and descriptor-based hardware-wallet
designs, including Ledger and BitBox02. Bitcoin Core provides the Bitcoin
functionality at its heart.

**Status: v2 is under development.** Docker can build a development image;
physical-hardware validation and release verification are ongoing.
Use test networks only; this project is not ready to protect mainnet funds.

## Claims we are building toward

- Your seed and private keys stay in RAM.
- Thunder Den saves no wallet state between boots.
- Thunder Den does not write to the boot USB or the laptop's disks.
- The running signer cannot access disk storage, Ethernet, Wi-Fi or Bluetooth.
- Wallet data and transactions move through QR codes.
- You review and approve transactions on the laptop before signing.
- Untrusted input stays within validated data interfaces; code changes, secret
  export and approval bypass are forbidden.
- You can rebuild the released USB image and compare its SHA-256.

## Implemented in the development image

- Import English BIP39 recovery words and an optional ASCII passphrase once per session.
- End a session to clear loaded keys, then power off or start a new session.
- Descriptor-based wallets, including SegWit and Taproot Miniscript.
- BIP-388 wallet policies with seed-bound registration proofs.
- BIP44, BIP49, BIP84 and BIP86 defaults without prior registration.
- Partial signing when other signatures or spending conditions are still needed.
- UR v2 scanning and animated QR output, with the scanner isolated from keys
  and approval.
- One USB image for x86-64 laptops with legacy BIOS or UEFI.
- A Docker/Compose build using pinned Linux containers with native build tools.

## What you trust

Thunder Den relies on your hardware and firmware, your build computer and pinned
dependencies such as Bitcoin Core and Linux. Reviewing this repository still
leaves those dependencies to review or trust.

Start with the [source-reading map](docs/DESIGN.md#build-and-independent-review),
the [dependency inventory](docs/DEPENDENCIES.md) and the
[build and test instructions](docs/BUILD.md).

Firmware behavior and physical attacks on memory are outside the software's
guarantees. RAM-only operation means no persistent wallet storage, not a promise
that every trace in physical memory becomes unrecoverable at power-off.

## Follow the work

- [Design](docs/DESIGN.md)
- [Scanner isolation](docs/ISOLATION.md)
- [Build and tests](docs/BUILD.md)
- [QR protocol](docs/PROTOCOL.md)
- [Dependencies](docs/DEPENDENCIES.md)
- [Implementation status](docs/STATUS.md)
- [QR bridge roadmap](docs/QR_BRIDGE_PLAN.md)

Tests and development tools run on the build computer. They are not part of the
production image. Software-wallet integrations and reference clients will follow
the signer protocol; no particular software wallet defines Thunder Den's design.

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
