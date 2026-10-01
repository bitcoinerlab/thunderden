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
- Choose the word count, then enter one lowercase word at a time. Words start
  hidden and are checked on Enter. Empty Enter does nothing. Up returns to the
  previous word while preserving the current draft; Backspace only edits the
  current word. Drafts must be validated with Enter before moving forwards.
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
Only after that process exits does the launcher invoke `--session-ended` in a
fresh process. This mode drops privileges and reuses the console renderer without
creating keys. It explains the cleanup and recommends a full shutdown when done.
Enter launches another fresh signer at network selection; each new session needs
recovery input when first used. EOF stops the launcher. This cleanup clears managed
secret buffers; it does not guarantee erasure of every trace in physical memory.

Only menu option **3** ends the session. Esc and Ctrl-C cancel operations but do
nothing at network selection, the main menu or the completion screen, so held
cancellation keys cannot clear a session or start another one.

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
Standard accounts are limited to account indices 0 through 100. The script-derivation
primitive permits any unhardened index, including existing inputs above 50,000.

Standard-account reviews show the actual account number. Master fingerprints
use the standard BIP32 meaning; they label keys but do not prove ownership.
Address positions are shown as receiving or change addresses with their index.
The exact descriptor paths remain visible, including custom receive/change paths.

Wallet applications should arrange backups of custom wallet configurations.
For multisig or custom Miniscript wallets, recovery words alone may not be enough
to reconstruct the wallet. Routine signer exports do not ask users to copy rules
from the device's screen.

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

### Direct multisig setup

The console can also load one public wallet setup from a `crypto-output` QR.
`wallet_qr.cpp` accepts sorted multisig inside legacy P2SH, nested SegWit
or native SegWit. During an inline setup scan, it also accepts `ur:bytes` containing
the public text multisig configuration described in [the protocol](PROTOCOL.md#text-multisig-setup).
That format supplies an M-of-N threshold, `P2WSH`, and a derivation/fingerprint/xpub
for each cosigner. Both codecs construct the same validated policy. This is a small,
bounded account importer, not a general descriptor language parser. Core remains
the script engine; no wallet-specific signing path is added.

When no approved policy can add signatures, the transaction layer can reconstruct
a standard sorted-multisig candidate directly from PSBT inputs. It validates the
redeem/witness-script hashes, threshold and sorted compressed child keys, then
maps every participating child key to a global account xpub through its exact
origin. Global xpubs are limited to 32 with origins at most 32 steps deep.
Relevant account versions, depth and child metadata must agree; each account
must derive the child key at the same receiving/change position. The local
account is rederived from the seed, and the complete constructed script must
match the input. The same `MultisigPolicy` constructor serves QR import and PSBT
discovery; it is not a second signing or descriptor engine.

A discovered definition is only a candidate. The existing review and `REGISTER`
approval must succeed before its HMAC is created or it becomes the loaded wallet.
The confirmation explicitly says that this does not sign the transaction yet.
Missing optional setup fields use the setup-scan fallback; invalid or
conflicting supplied data is rejected. Multiple different new wallet definitions
are not silently combined: the user supplies the intended setup QR instead.
PSBT-derived account keys are put in canonical order so address-index-dependent
sorting does not create different candidate identities for one wallet.

The importer requires complete public key origins: Sparrow's `m/45h` legacy
layout or the corresponding hardened BIP48 account path. Each cosigner may use a
different account number. Sparrow omits child paths in this QR, so Thunder Den
explicitly constructs and reviews `/0/*` receiving and `/1/*` change branches.
Explicit child paths are rejected by this import rather than silently changed.
Names and notes in the QR are bounded and ignored. A generated wallet label is
for readability; the exact policy and key vector establish wallet identity.

The signer parses this data independently of the scanner. The existing
registration gate checks local ownership, shows every cosigner key and obtains
typed `REGISTER` approval before generating a proof. The approved policy and
proof stay together in one RAM-only slot. A failed or declined replacement keeps
the old slot. Ending the session removes it. The underlying HMAC does not expire
at logout; existing command clients still use their saved proofs normally.

Our key can be verified from the seed. Other cosigners must be checked against
their devices or a trusted backup. Comparing two displays fed by the same
compromised coordinator does not authenticate those cosigners. After reboot,
fresh approval of a substituted setup is not proof that it is the old wallet.

## Signing

`ReviewedTransaction` retains an immutable decoded PSBTv0, its authorized policy
and the review facts. Candidate reviews share one validated request snapshot and
its precomputed signing data; they do not repeatedly decode or retain separate
copies of the entire PSBT. Copies of a policy share immutable parsed descriptors,
with independent policy names/key vectors, rather than reparsing the descriptor.
The request accepts at most 1 MiB of PSBT data and 128 inputs/outputs each. Core checks
the unsigned transaction; Thunder Den rejects conflicting previous-output data,
out-of-range amounts, unsupported signing rules and invalid finalized inputs.

Every input needs a usable previous output. Full previous transactions are checked
against the referenced txid and output index; conflicting witness UTXOs are
rejected. Legacy and unclassified input types require full previous transactions.
Compact data is accepted only for recognized native SegWit-v0, wrapped SegWit-v0
or native Taproot outputs. Wrapped inputs require a redeem script whose hash
matches the supplied output; our own scripts come from the authorized policy.

The review records which inputs have full previous transactions. Fee assurance is
checked for each eligible signing input: legacy ALL commits to no input amounts,
SegWit-v0 ALL commits to its own amount, and Taproot DEFAULT commits to every input
amount and script. If any signature from the selected policy could leave a missing
amount unauthenticated, the fee is marked unverified. This includes external
inputs. A single-input SegWit-v0 spend can use compact data safely against this fee
attack: a wrong amount makes its signature invalid. Taproot DEFAULT likewise
protects all amounts, including in a mixed-input transaction.

An unverified-fee request requires an explicit red warning confirmation followed
by the normal review and typed `SIGN`. The warning is not an error notice: Esc
cancels, and navigation alone does not accept the last warning page. The red
heading and qualified fee/input totals persist through Details, final confirmation
and revisiting the reply. Accepting the warning does not authenticate the missing
amounts or stop the known multi-input fee attack. [Fee guidance](FEES.md) explains
the tradeoff in plain English.

Derivation hints propose address positions; only an exact match against the
authorized policy's derived script establishes ownership. Receive/self-payment
and change branches remain distinct. An unrecognized output is displayed as an
external destination, even when the request labels it as change. Unrecognized
inputs contribute to the fee calculation and are disclosed but not signed.
Each input/output permits at most 128 derivation hints and 16 candidate positions.
Supplied redeem/witness scripts and Taproot commitments are checked against Core's
public descriptor expansion before approval.

Plain PSBTs suggest standard account candidates through their origins. Only
BIP44/49/84/86 paths within the existing account bounds are considered, with a
maximum of eight candidate accounts plus the loaded multisig wallet. Fingerprints
and paths are hints; exact locally derived scripts establish a match. Public-only
completion checks and verification of existing local signatures filter out wallets
that have nothing to add. No transaction signature is made during detection.

One match goes directly to review. Multiple matches produce a chooser, then the
same review. Each pass signs one policy and preserves existing signatures; it
never falls back silently to another policy. Missing multisig setup can be loaded
from the PSBT's complete verified data or a setup QR while retaining an owned
copy of the original PSBT. Setup approval and
transaction approval remain separate. See the [multi-account example](SPARROW.md#transactions-using-more-than-one-wallet).

The immutable review exposes wallet identity, network, input outpoints and amounts,
destinations or raw output scripts, wallet flow, fee, sequences, locktime and
per-input signing rules. A weight-based size estimate uses Core's dummy signatures;
unverified request signatures cannot shrink it. The estimate is unavailable when
an unfinished external input or unsatisfied script prevents dummy finalization.

The mandatory signing review shows wallet/network, every non-change destination's
full address or script and amount, the fee with its assurance, a fee-rate estimate when
available, and verified change totals/counts. Receive/self-payments remain visible.
Only independently verified change addresses move to Details. With unrecognized
inputs, wallet accounting is explicitly limited to verified inputs/outputs rather
than claiming an exact wallet decrease. Active absolute and relative lock conditions
are disclosed without claiming chain maturity. Input records, individual change
addresses, raw transaction fields and wallet-policy identity remain in Details.

`Sign` invokes a local approval callback with these facts. Rejection returns no
result. Private signing providers are created only after approval, and the seed
and network must match the review. Owned non-Taproot inputs use SIGHASH_ALL;
owned Taproot inputs use SIGHASH_DEFAULT. Core adds signatures to a copy of the
reviewed PSBT, preserving the unsigned transaction. The returned PSBT is limited
to 2 MiB and may still require other signers or preimages.

Core finalizes a separate copy to determine completeness and verifies its input
scripts when complete. The result reports new signature count and completeness;
it does not establish current-chain validity or timelock maturity.

The local interface wraps full addresses/scripts onto review pages. Every required
review page must be traversed before a separate typed `SIGN` confirmation is
accepted. Queued input is discarded at screen/approval boundaries, and terminal
resizing aborts the review. Registration uses a separate `REGISTER` confirmation.
Public-key exports show the
complete key, origin and standard address type in the required review, with no
Details toggle or encoding label. A sentence
above the controls explains when to press Enter, separated from the review by
a blank line. Recovery words and passphrases start hidden, with one asterisk per
character in the visible part of the field. The visibility status follows the
explanation, with its TAB instruction on the next line above the field. Word visibility
persists within recovery entry; passphrase entry and confirmation each start
hidden. Both use secure buffers, and rejected words are cleared instead of
restoring an earlier value. A blank line precedes every input field.
TAB toggles immediately for each key event. Up preserves a recovery-word draft
without accepting it; only Enter on a valid, non-empty word moves forwards.
Validation errors use the console's error color as well as a text message.
Single-page reviews omit the pager; multi-page counters appear at the right of
the bottom divider, separately from the actions. `d` opens details and returns
to the same summary page without approving the operation. For signing and
registration, Details contain only technical information; their final Enter
returns to the required review and cannot authorize the operation. Address-confirmation
Details include the short summary context. Esc cancels
an unapproved operation from either view. Both views share one navigation loop.

Completed results retain their public review and exact QR reply in memory until
the user finishes viewing them. `b` or Left on the QR opens the completed text
review; its final Enter shows the same reply again. This display loop has no
signing/registration callback. Completed signing reviews are labelled as signed,
and Esc means Finish, not undo. Connection/error replies without a matching
successful review offer only Finish. Enter does not dismiss a QR, and animated
QRs advance automatically without a pause control.

The console uses a dark palette, an 80-column reading area and word-wrapped
explanations. Menus accept arrows and Enter alongside numbered shortcuts. Moving
the selection repaints only the affected rows, not the whole screen. A
bounded decoder consumes arrow-key sequences without treating them as Escape or
input text. Escape and Ctrl-C repeats are suppressed across screen transitions
until there has been a one-second gap or a different key is pressed. This keeps
a held cancellation key from also cancelling the parent screen. All styling
comes from the renderer; wallet-provided text is still printable ASCII only.
Menus, inputs and reviews prepare their body rows using their own layout.
The renderer draws those rows without rewrapping them or starting another
interactive screen. Text validation is separate from wrapping.
Long menu labels wrap within their numbered option on narrow screens. The main
menu's orange loaded-wallet status uses the otherwise blank header row, so it
remains visible even when the options scroll. An overlong status is visibly
shortened with an ellipsis; the complete wallet definition stays in the review.

GRUB uses the firmware's automatic graphics mode. Before loading keys or dropping
privileges, the signer selects a built-in Terminus font when the screen is large
enough, with the compact VGA font as a fallback. High-DPI screens use an exact
2x enlargement of the larger bitmap. Font and palette setup are optional visual
enhancements; unsupported ioctls retain the existing console settings. The QR
and camera renderer reads the chosen font with bounded buffers and reserves room
for complete instructions. QR modules remain black on white with a quiet border.

## QR exchange

The [protocol](PROTOCOL.md) uses UR v2, including animated fountain-coded messages.
Standard PSBT exchange uses `crypto-psbt`. Standard public-key shortcuts use SeedSigner's
`crypto-account` format: a master fingerprint and one script-typed public account
key. This is a cosigner export, not a complete multisig configuration. It uses
the deployed legacy registry tags and shares the HD-key encoder with modern
`hdkey` exports.

The main menu has three actions: scan a QR, share a public key and end the session.
There is no standalone descriptor export. Sparrow can assemble both single-key
and multisig wallets from account-key QRs, and Liana retrieves keys through its
commands. Descriptors remain the internal wallet model and are visible in policy
Details; a multisig setup descriptor can still be scanned when needed.

Sparrow's airgapped and watch-only importers preserve the key and its origin
through `crypto-account`. Sparrow 2.3.1/2.5.5 can decode standalone `ur:hdkey`, but
the watch-only handler copies only its key and the airgapped import screen does
not handle that result. Static `[origin]xpub` works in Sparrow too, but some other
wallets strip its origin. The account format therefore carries the intent explicitly.
This is an interoperability choice, not a different derivation or signing rule.

Standard shortcuts have no format prompt. Custom paths cannot reliably identify
an account type, so the advanced flow asks for a path and offers only public-key
text or modern `ur:hdkey`. It never invents a script type for an arbitrary path.
The advanced menu selects the encoding; the public-key review shows the complete
key and path. Liana obtains xpubs through
`GET_XPUB` command replies, independently of these manual exports.

Incoming `crypto-output` carries the bounded multisig setup described above.
Plain PSBT input uses verified automatic matching. Named policy commands use
explicit versioned requests carrying the complete wallet definition and proof.
The [Thunder Den commands](PROTOCOL.md#thunder-den-commands) and UR transport
share a bounded CBOR reader. It rejects non-minimal encodings and checks lengths
before slicing data. Commands carry raw PSBT bytes, and a request ID matches each
reply to its operation. Fingerprints label keys; full xpub comparison and
registration HMACs establish ownership and wallet authorization.
Public-key derivation returns only `CExtPubKey` through `Keys::PublicAt()`; its
temporary private key and chain code are cleared before returning.

Captured frames are bounded and their row stride is honored. Incoming multipart
messages must keep consistent types, lengths, counts and checksums; conflicting
streams never silently replace scan state. Outgoing animation keeps a fixed QR
geometry and a four-module quiet border. Static public-key QRs are sized from
their actual case-sensitive payload. QR results share the same review/Finish
controls; animation advances automatically.

Scanning continues while complete previews arrive. Ten seconds without a complete
preview or result ends the scan with a timeout message. The preview controls
banner is redrawn only when its progress text changes.

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

The main code boundaries are deliberately small:

- `keys` owns secret derivation and public-key/path encoding.
- `policy` owns wallet definitions, standard defaults and authorization.
- `transaction` validates one immutable request, finds matching policies and
  produces policy-specific reviews and signatures. It does not depend on the UI.
- `wallet_qr` translates public wallet formats into and out of those models.
- `application` coordinates setup and signing with local approval; `main` owns
  the session and routes user actions. `review` supplies text, and `terminal` /
  `hardware` render it. QR commands reuse the same approval and signing paths.

No format importer or scanner can create approval. The important boundary is the
local confirmation of a validated wallet or immutable transaction review.

One pinned Docker environment supplies the build and test tools. Source archives
are hash-checked. The release build must normalize final disk metadata and produce
byte-identical images from clean outputs.

Tests and development tools stay outside the signer image. For an AI-assisted
audit of Thunder Den's own code, start with:

- `src/main.cpp`, `src/application.cpp`, `src/terminal.cpp`, `src/review.cpp` and `src/hardware.cpp`:
  recovery input, session lifetime, what is displayed and how consent is obtained.
- `src/wallet_qr.cpp`, `src/policy.cpp`, `src/transaction.cpp` and `src/keys.cpp`:
  request validation, wallet authorization, immutable review and key use.
- `src/scanner_main.cpp`, `src/camera.cpp`, `src/transport.cpp`, `src/scan.cpp` and
  `src/isolation.cpp`: image/QR decoding, input bounds and the process boundary.
- `.env`, `CMakeLists.txt`, `build/` and `platform/`: pinned inputs, linked code,
  installed contents and operating-system permissions and restrictions.
- `tests/` and [Implementation status](STATUS.md): what is checked and what still
  needs verification.

See [Dependencies](DEPENDENCIES.md#trust-boundary) for the upstream software you
also need to review or trust.

The Liana fork and QR bridge implement wallet integration using this protocol.
See [Liana integration](STATUS.md#liana-integration) for current support.
