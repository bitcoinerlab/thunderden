# Design

Thunder Den turns an x86-64 laptop into a RAM-only, QR-connected Bitcoin signer.
The implementation is designed for independent source review and reproducible
builds. Published claims apply only to a verified release, not unfinished code.

## Runtime

The bootloader loads Linux and an embedded RAM filesystem. Persistent storage
and external networking are unavailable to the signer. Keyboard, camera and
screen provide its user interface. Hardware and firmware behavior remain outside
the software's guarantees.

The native library links Bitcoin Core's pinned internal key, descriptor, Miniscript,
and PSBT implementations. It uses Core's validation, derivation, serialization,
cryptographic primitives, and secure allocation. OpenSSL supplies PBKDF2 and
constant-time comparison through library calls. Operations run directly in-process,
without a Bitcoin daemon, RPC interface, or node database.

Core's APIs are internal: upgrades require rebuilding and testing the complete
integration. Unused functions are removed by the linker. Core RNG initialization
retains system-metadata reads and attempts a local netlink query. Derivation and
signing tests pass when socket creation is unavailable. The image configuration
disables the kernel networking and block-device subsystems entirely.

The native application uses the local Linux console for keyboard input and review.
Camera preview and QR output use the framebuffer, including the kernel's console
font for status captions. libv4l supplies webcam frames, ZBar reads QR images, and
libqrencode generates them. There is no desktop compositor or GUI framework.

## Recovery input

- English BIP39 wordlist; 12, 15, 18, 21, or 24 words with a valid checksum.
- Optional passphrase of printable ASCII characters, including spaces.
- Passphrase case and every space are significant. Unsupported bytes are rejected.
- The mnemonic is entered when first needed. Derived keys remain in RAM for the session.
- No mnemonic generation or persistent seed storage.
- The initial console interface uses the kernel's default US keyboard layout.

ASCII is unchanged by BIP39 NFKD normalization. Non-English mnemonics and
non-ASCII passphrases are an explicit compatibility limitation.

## Wallets

All accounts use BIP-388 descriptor templates and ordered key-information vectors.
Core interprets the expanded descriptors and Miniscript. The application checks
key references, policy restrictions, seed ownership, authorization, and the actual
transaction scripts. It does not implement a second Miniscript engine.

Exact BIP44/49/84/86 defaults with standard origins and verified account xpubs
do not require registration. Other supported policies require a named registration.
The same engine handles multisig and SegWit/Taproot Miniscript. MuSig2 is outside
the initial scope. Unsupported functions and excessive resource use are rejected.

The native adapter parses key references and paths, then checks Core's parsed key
count and public-key set against the supplied vector. Core handles nested script
semantics and exposes warnings directly. Warning-bearing descriptors are rejected.

Current library limits are 32 keys, 128 references, 8,192 template characters,
65,536 expanded descriptor characters, 64 nesting levels, 64 Miniscript wrappers,
and 32 origin-path steps. Names contain at most 64 printable ASCII characters
without leading/trailing spaces. Passphrases contain at most 128 characters.
Default accounts are limited to account indices 0 through 100. The script-derivation
primitive permits any unhardened index, including existing inputs above 50,000.

## Registration

1. Receive the complete named BIP-388 policy.
2. Validate its structure and resolve the descriptors through Core.
3. Rederive local keys; fingerprint matches alone are not ownership proof.
4. Show the name, exact template, all key origins/xpubs, and a receive address.
5. Obtain approval and return a wallet ID and registration HMAC.

Wallet IDs use Ledger version-2 serialization: name, template length/hash, and
the ordered key-vector Merkle root. The exact approved bytes are authenticated;
policy equivalence and rewriting are not part of registration.

The HMAC key is derived from the BIP39 seed using SLIP-0021 with application label
`Thunder Den wallet policy`. The proof is HMAC-SHA256 over the wallet ID.
Proofs are seed-bound, deterministic, and non-revocable. A custom wallet still
requires a backup of its full policy.

Software wallets retain the policy and proof and supply them for later operations.
They are untrusted data sources. Registration authenticates the wallet definition;
every transaction still requires its own review and approval.

## Signing

`ReviewedTransaction` owns a decoded PSBTv0, its authorized policy, and the review
facts. It accepts at most 1 MiB of PSBT data and 128 inputs/outputs each. Core checks
the unsigned transaction; the adapter rejects conflicting previous-output data,
out-of-range amounts, unsupported signing rules, and invalid finalized inputs.

Every input requires its full previous transaction unless all inputs are Taproot.
Legacy and SegWit-v0 signatures do not commit to other inputs' amounts; trusting
those amounts alone would permit misleading fee review. All-Taproot transactions
may use witness UTXOs because the selected Taproot signing rule commits to every
input's amount and script.

Derivation hints propose address positions; only an exact match against the
authorized policy's derived script establishes ownership. Receive/self-payment
and change branches remain distinct. An unrecognized output is displayed as an
external destination, even when the request labels it as change. Unrecognized
inputs contribute to the fee calculation and are disclosed but not signed.
Each input/output permits at most 128 derivation hints and 16 candidate positions.
Supplied redeem/witness scripts and Taproot commitments are checked against Core's
public descriptor expansion before approval.

The immutable review exposes wallet identity, network, input outpoints and amounts,
destinations or raw output scripts, wallet flow, fee, sequences, locktime, and
per-input signing rules. A weight-based size estimate uses Core's dummy signatures;
unverified request signatures cannot shrink it. The estimate is unavailable when
an unfinished external input or unsatisfied script prevents dummy finalization.

`Sign` invokes a local approval callback with these facts. Rejection returns no
result. Private signing providers are created only after approval, and the seed
and network must match the review. Owned non-Taproot inputs use SIGHASH_ALL;
owned Taproot inputs use SIGHASH_DEFAULT. Core adds signatures to a copy of the
reviewed PSBT, preserving the unsigned transaction. The returned PSBT is limited
to 2 MiB and may still require other signers or preimages.

Core finalizes a separate copy to determine completeness and verifies its input
scripts when complete. The result reports new signature count and completeness;
it does not establish current-chain validity or timelock maturity.

The local interface wraps full addresses/scripts onto review pages. Every page
must be traversed before a separate typed `SIGN` confirmation is accepted. Queued
input is discarded at screen/approval boundaries, and terminal resizing aborts
the review. Registration and public-account export use separate `REGISTER` and
`EXPORT` confirmations. Mnemonic/passphrase entry is masked and uses secure buffers.

## QR exchange

The [protocol](PROTOCOL.md) uses UR v2, including animated fountain-coded messages.
Standard PSBT exchange uses `crypto-psbt`; public default-account export uses
`crypto-account`. Plain PSBT input prompts for a local default account, which is
constructed and checked by the same policy engine. Named policy operations use
explicit versioned requests carrying the complete wallet definition and proof.

Captured frames are bounded and their row stride is honored. Incoming multipart
messages must keep consistent types, lengths, counts, and checksums; conflicting
streams never silently replace scan state. Outgoing animation keeps a fixed QR
geometry and a four-module quiet border, with pause and cancellation controls.

## Input trust boundary

The security requirement covers keyboard input, camera images, decoded QR data,
and every future input channel. Data must pass bounded, typed interfaces and must
never be executed as application/shell code or given authority to replace signer
code. There is no seed/private-key export command. Imported data cannot supply
local consent: signing requires approval of the exact immutable transaction review.

Current controls include size/depth limits, strict schemas, duplicate-field and
stream-conflict rejection, printable review text, and separate local confirmations.
These controls address protocol and terminal injection and authorization bypass in
the exercised paths.

The current development application shares an address space with the camera/QR
libraries and runs as root in the image. Isolating untrusted decoding from keys and
approval state, and enforcing least privilege and non-writable application files,
remain required hardening work. Validation and passing tests do not establish that
native-parser memory-corruption bugs are impossible.

## Build and audit

One pinned Docker environment supplies the build and test tools. Source archives
are hash-checked. The release build must normalize final disk metadata and produce
byte-identical images from clean outputs.

Tests and development tools stay outside the production installation list. Each
milestone includes source review, behavior checks, and a dependency/size review.
Software-wallet integration clients are a later deliverable using the documented
protocol and shared vectors.
