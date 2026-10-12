# Project status

Thunder Den is under active development. The latest published USB image is
[`v0.0.1-preview.6`](https://github.com/bitcoinerlab/thunderden/releases/tag/v0.0.1-preview.6).
The `feature/seed-ocr` branch adds experimental camera-assisted recovery entry;
that feature is not included in preview 6.

This page describes current capabilities and verification scope. Release-specific
artifacts, checksums and changes belong in the [release notes](https://github.com/bitcoinerlab/thunderden/releases).

## Capabilities

| Area | Implemented behavior |
| --- | --- |
| Recovery input | English BIP39, 12/15/18/21/24 words; printable ASCII passphrase |
| Key sessions | Keys stay in RAM until logout or reboot |
| Wallets | Standard single-signature accounts, sorted multisig and supported Miniscript policies |
| Registration | Local wallet review, key-ownership checks and typed `REGISTER` approval |
| Signing | Immutable PSBT review, explicit `SIGN`, signature preservation and partial signing |
| Fees | Verified or explicitly qualified fee accounting for compact SegWit/Taproot requests |
| Transport | UR v2 PSBTs, account exports and wallet-independent QR commands |
| Boot | 64 MiB x86-64 BIOS/UEFI image, running without network or disk-storage support |
| Experimental OCR | Isolated CPU recognition, BIP39-ranked suggestions and a local word editor |

The implementation uses pinned, unmodified Bitcoin Core libraries for key,
descriptor, Miniscript and transaction operations. It does not run a Bitcoin
daemon or node database. See [design](DESIGN.md), [fees](FEES.md),
[protocol](PROTOCOL.md) and [isolation](ISOLATION.md) for exact boundaries.

## Wallet integrations

### Sparrow integration

Sparrow uses the **SeedSigner** keystore profile for Thunder Den's account QRs.
The workflow supports single signature and standard sorted multisig, with
PSBT-derived registration when sufficient setup data is included. Otherwise it
requests a separate setup QR. See the [Sparrow guide](SPARROW.md).

Independent software checks have covered Sparrow 2.5.5 account import,
descriptor/address agreement and synthetic signing round trips across legacy,
nested SegWit and native SegWit multisig. Earlier descriptor-import checks also
covered Sparrow 2.3.1. These results do not certify every coordinator version,
physical camera or broadcast path.

### Text multisig setup and Keeper integration

Keeper uses its **Jade → QR** profile. Standard account QRs and P2WSH text
multisig setup inside `ur:bytes` are supported. Setup can be scanned from the main
menu or requested during signing. `REGISTER` remains separate from `SIGN`.
See the [Keeper guide](KEEPER.md).

Native coverage includes structured/text setup equivalence, supported thresholds,
network/origin validation, malformed input, cancellation, wallet replacement,
preloading and successive cosigner passes. Version-specific coordinator behavior
and physical QR transfer require integration testing beyond those fixtures.

### Liana integration

Thunder Den support is available in the
[Liana fork](https://github.com/bitcoinerlab/wizardsardine-liana), using the
[async-hwi fork](https://github.com/bitcoinerlab/wizardsardine-async-hwi) and
[`@bitcoinerlab/thunderden-qr-bridge`](https://github.com/bitcoinerlab/thunderden-qr-bridge).
The regular desktop integration supports Bitcoin Core and Electrum backends.
It has not been submitted upstream and is not included in standard Liana releases.

Software checks cover key retrieval, registration, address verification and
primary/recovery spending for P2WSH and Taproot Miniscript. Liana Connect support
is disabled pending a compatible registration-token server API; Liana Business
end-to-end support is separate work.

## Experimental camera entry

The [OCR guide](OCR.md) describes capture, correction and supported writing
formats. The feature includes a 30-slot editable draft, probability-ranked
BIP39 suggestions and a checksum search bounded to 64 retained combinations.
Only a valid BIP39 phrase can proceed to passphrase entry.

The recognizer is a separate confined process. It sees camera frames but has no
signer memory, keyboard or approval authority. OpenCV is limited to core,
image-processing and CPU inference modules; build-time Python and ONNX Runtime
are absent from the device image.

Generated layout fixtures cover punctuation, underlines, word/character boxes,
column layouts, headings and blank cards. They establish reproducible structural
behavior, not broad handwriting accuracy or hardware compatibility. The fixture
sources and limitations are in [`tests/fixtures/ocr`](../tests/fixtures/ocr/README.md).

## Verified native behavior

Run the current suite with:

```sh
docker compose --progress quiet run --build --rm test
```

The experimental branch defines fourteen suites: `seed-ocr`, `ocr-engine`,
`ocr-templates`, `qr-commands`, `foundation`, `transactions`, `core-keys`,
`native-runtime`, `transport`, `application`, `export-compat`, `terminal`,
`camera` and `isolation`.

| Coverage | Reproducible sources |
| --- | --- |
| BIP39/BIP32 vectors, key derivation and passphrase handling | `tests/foundation.cpp`, `tests/core_keys.py` |
| Policy validation, wallet IDs, registration tags and key ownership | `tests/foundation.cpp`, `tests/application.cpp` |
| PSBT signing, script verification, fee assurance and immutable review | `tests/transactions.cpp` |
| UR vectors, malformed input, account exports and interoperability | `tests/transport.cpp`, `tests/export_compat.py` |
| Local approval, editing, cancellation and session lifecycle | `tests/terminal.py` |
| Camera conversion, preview and device failures | `tests/camera.cpp` |
| Scanner IPC bounds, confinement and process isolation | `tests/isolation.cpp`, `tests/native_runtime.py` |
| OCR decoding, templates, ordering and draft validation | `tests/seed_ocr.cpp`, `tests/ocr_engine.cpp`, `tests/ocr_templates.cpp` |

Confinement tests require Landlock ABI 6 and report a skip on older hosts; the
application itself never falls back to an unconfined scanner. Transaction fixtures
use synthetic previous transactions and Core script verification, not chain or
mempool acceptance.

## Verified image behavior

The [build guide](BUILD.md) provides commands for these checks:

- Pinned source verification, including the signed Buildroot checksum announcement.
- Kernel restrictions: networking, disk storage, swap, modules and crash persistence.
- Packed-initramfs file hashes, ownership, permissions and absence of development tools.
- Repeat assembly with changed source timestamps and time zones.
- BIOS/UEFI QEMU boot, local entry, public account QR decoding and session restart.
- Scanner containment on the image kernel/runtime using a test-only overlay.

A historical full clean-build comparison at source revision
`e4c07d45348b94e2035d16f52bf97f9cffea0a9c` produced identical artifacts from
separate uncached builders and fresh volumes. Repeat assembly is a narrower
check and does not replace that procedure for later revisions. See
[clean-build comparison](BUILD.md#clean-build-comparison).

## Remaining validation

- Broader physical laptop/webcam coverage, including slow cameras and reconnects.
- A writer-diverse, camera-captured OCR benchmark with independent ground truth.
- Release-candidate checks against the exact source revision and build inputs.
- A fresh full clean-build comparison for the current release candidate.
- Coordinator/version interoperability beyond the documented workflows.

Synthetic images, pseudo-terminals and emulated displays are useful regression
tests, but do not establish physical camera compatibility or transcription accuracy.
