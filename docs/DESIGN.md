# Design

Thunder Den turns an x86-64 laptop into a RAM-only, QR-connected Bitcoin signer.
The code and build process are designed to be easy to audit with AI.
[Implementation status](STATUS.md) lists completed checks and what still needs
verification.

## Runtime

At boot, the laptop loads Linux and the signer into RAM from the USB drive. Linux
is built without networking or disk-storage support, so the running signer cannot
use the laptop's disks or network connections. Hardware and firmware remain part
of what you trust.

Thunder Den's signing program is written in C++. It uses Bitcoin Core's code for
key derivation, wallet descriptors, Miniscript and PSBT signing. That code runs
inside the signer, without a Bitcoin node or blockchain database.

Bitcoin Core does not provide a stable public wallet library for other applications.
Thunder Den uses Core's internal interfaces and builds against a specific version.
Upgrading Core may require changes to Thunder Den and always requires rebuilding
and testing the signer.
The [dependency guide](DEPENDENCIES.md) describes the libraries included in the build.

You enter recovery words and review transactions using the laptop's keyboard and
screen. A separate scanner program reads QR codes from the camera and passes the
decoded data to the signer. The signer holds the keys and asks for local approval
before signing. [Scanner isolation](ISOLATION.md) explains how the two programs
are kept separate.

## Recovery input

- English BIP39 wordlist; 12, 15, 18, 21 or 24 words with a valid checksum.
- Choose the word count, then enter one lowercase word at a time. Words are visible
  and checked against the wordlist immediately. An empty entry goes back one word.
- The complete mnemonic's checksum is checked before asking for a passphrase.
  If it fails, the attempt is cleared and entry restarts at word 1 with the same
  word count.
- Optional passphrase of printable ASCII characters, including spaces.
- Passphrase case and every space are significant. Unsupported bytes are rejected.
- An empty passphrase needs one Enter. A non-empty passphrase must be entered twice;
  a mismatch lets you retry without re-entering the recovery words.
- The mnemonic is entered when first needed. Derived keys remain in RAM for the session.
- No mnemonic generation or persistent seed storage.
- The initial console interface uses the kernel's default US keyboard layout.

ASCII is unchanged by BIP39 NFKD normalization. Non-English mnemonics and
non-ASCII passphrases are an explicit compatibility limitation.

Recovery-word, passphrase and intermediate BIP39 seed buffers are zeroed before
release after key derivation. The master private key, chain code and wallet-policy
registration key remain available for the session.

**End session (clear keys)** destroys the key session and exits the signer normally.
Only after that process exits does the launcher display **Session ended / Loaded
keys cleared**. The laptop stays on at this screen. The user can turn it off or
press Enter to launch a fresh signer at network selection. Each new session
requires recovery input when first needed. This cleanup clears managed secret
buffers; it does not guarantee erasure of every trace in physical memory.

Only menu option **3** ends the session. Esc and Ctrl-C cancel operations but do
nothing at network selection or the main menu, so held cancellation keys cannot
clear the session after returning from an operation.

## Wallets

All accounts use BIP-388 descriptor templates and ordered key-information vectors.
Core interprets the expanded descriptors and Miniscript. The application checks
key references, policy restrictions, seed ownership, authorization and the actual
transaction scripts. It does not implement a second Miniscript engine.

Exact BIP44/49/84/86 defaults with standard origins and verified account xpubs
do not require registration. Other supported policies require a named registration.
The same engine handles multisig and SegWit/Taproot Miniscript. MuSig2 is outside
the initial scope. Unsupported functions and excessive resource use are rejected.

Thunder Den parses key references and paths, then checks Core's parsed key
count and public-key set against the supplied vector. Core handles nested script
semantics and exposes warnings directly. Warning-bearing descriptors are rejected.

Thunder Den limits policies to 32 keys, 512 characters per key-information string,
128 references, 8,192 template characters, 65,536 expanded descriptor characters,
64 nesting levels, 64 Miniscript wrappers and 32 origin-path steps.
Names contain at most 64 printable ASCII characters without leading/trailing
spaces. Passphrases contain at most 128 characters.
Default accounts are limited to account indices 0 through 100. The script-derivation
primitive permits any unhardened index, including existing inputs above 50,000.

## Registration

1. Receive the complete named BIP-388 policy.
2. Validate its structure and resolve the descriptors through Core.
3. Rederive local keys; fingerprint matches alone are not ownership proof.
4. Show the name, exact template, all key origins/xpubs and a receive address.
5. Obtain approval and return a wallet ID and registration HMAC.

Wallet IDs use Ledger version-2 serialization: name, template length/hash and
the ordered key-vector Merkle root. The exact approved bytes are authenticated;
policy equivalence and rewriting are not part of registration.

The HMAC key is derived from the BIP39 seed using SLIP-0021 with application label
`Thunder Den wallet policy`. The proof is HMAC-SHA256 over the wallet ID.
Proofs are seed-bound, deterministic and non-revocable. A custom wallet still
requires a backup of its full policy.

Software wallets retain the policy and proof and supply them for later operations.
They are untrusted data sources. Registration authenticates the wallet definition;
every transaction still requires its own review and approval.

## Signing

`ReviewedTransaction` owns a decoded PSBTv0, its authorized policy and the review
facts. It accepts at most 1 MiB of PSBT data and 128 inputs/outputs each. Core checks
the unsigned transaction; Thunder Den rejects conflicting previous-output data,
out-of-range amounts, unsupported signing rules and invalid finalized inputs.

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
destinations or raw output scripts, wallet flow, fee, sequences, locktime and
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
`EXPORT` confirmations. Recovery words are visible during entry and passphrases are
masked. Both use secure buffers.

## QR exchange

The [protocol](PROTOCOL.md) uses UR v2, including animated fountain-coded messages.
Standard PSBT exchange uses `crypto-psbt`; public default-account export uses
`crypto-account`. Plain PSBT input prompts for a local default account, which is
constructed and checked by the same policy engine. Named policy operations use
explicit versioned requests carrying the complete wallet definition and proof.

Captured frames are bounded and their row stride is honored. Incoming multipart
messages must keep consistent types, lengths, counts and checksums; conflicting
streams never silently replace scan state. Outgoing animation keeps a fixed QR
geometry and a four-module quiet border. Animated QR codes have Space to pause or
resume; static codes show only Esc to go back and ignore Space.

## Input trust boundary

The security requirement covers keyboard input, camera images, decoded QR data
and every future input channel. Data must pass bounded, typed interfaces and must
never be executed as application/shell code or given authority to replace signer
code. There is no seed/private-key export command. Imported data cannot supply
local consent: signing requires approval of the exact immutable transaction review.

Current controls include size/depth limits, strict schemas, duplicate-field and
stream-conflict rejection, printable review text and separate local confirmations.
These controls address protocol and terminal injection and authorization bypass in
the exercised paths.

[Scanner isolation](ISOLATION.md) adds defense in depth against exploitation of
image/QR decoding bugs through attacker-controlled input. Decoding runs in a fresh,
unprivileged process with filesystem/process restrictions. The main process retains
the keys and local approval devices and validates the scanner's bounded output.
Application files remain root-owned. Core's parsers, the review/signing logic,
Linux and hardware remain trust dependencies. The isolation guide explains the
threat model, enforced boundary and practical limits.

## Build and independent review

One pinned Docker environment supplies the build and test tools. Source archives
are hash-checked. The release build must normalize final disk metadata and produce
byte-identical images from clean outputs.

Tests and development tools stay outside the signer image. For an AI-assisted
audit of Thunder Den's own code, start with:

- `src/main.cpp`, `src/terminal.cpp`, `src/review.cpp` and `src/hardware.cpp`:
  recovery input, session lifetime, what is displayed and how consent is obtained.
- `src/application.cpp`, `src/policy.cpp`, `src/transaction.cpp` and `src/keys.cpp`:
  request validation, wallet authorization, immutable review and key use.
- `src/scanner_main.cpp`, `src/camera.cpp`, `src/transport.cpp`, `src/scan.cpp` and
  `src/isolation.cpp`: image/QR decoding, input bounds and the process boundary.
- `.env`, `CMakeLists.txt`, `build/` and `platform/`: pinned inputs, linked code,
  installed contents and operating-system permissions and restrictions.
- `tests/` and [Implementation status](STATUS.md): what is checked and what still
  needs verification.

See [Dependencies](DEPENDENCIES.md#trust-boundary) for the upstream software you
also need to review or trust.

Software-wallet integration clients are a later deliverable using the documented
protocol and shared vectors.
