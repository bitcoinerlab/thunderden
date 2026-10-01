# Thunder Den QR protocol

Thunder Den defines these message formats independently of any particular wallet
or companion program. Any client implementing them can exchange QR messages with
the signer. There is no network server on the offline laptop.

The application uses UR v2 with Bytewords-minimal encoding and fountain-coded
multipart messages. PSBTs remain version 0. BBQR and legacy multipart formats are
not supported. Outgoing UR text is uppercase for QR alphanumeric encoding.
Custom public-key exports may also use a case-sensitive static text QR.

| Message | UR type | Purpose |
| --- | --- | --- |
| Plain PSBT | `crypto-psbt` | Standard transaction exchange with a verified matching account or approved wallet |
| Thunder Den command | `bytes` | Information, xpub retrieval, registration, address checks and policy-based signing |
| Standard account key | `crypto-account` | One-way, script-typed public cosigner key and origin |
| Custom public key | Plain text QR or `hdkey` | Public extended key and origin, without script-type assumptions |
| Multisig setup | `crypto-output`, or `bytes` during inline setup | Import and locally approve a supported public multisig account |

## Human-operated exchange

The user chooses when to scan on Thunder Den, reviews the requested operation and
approves locally when required. The signer then displays the reply QR for the
online client to scan. Animated frames are collected automatically within each
scan. The computers can be repositioned between steps; continuous camera alignment
is not required. Recovery input stays on the signer and should be completed before
the online camera is pointed at its screen.

## Standard PSBT exchange

`ur:crypto-psbt` contains a CBOR byte string holding the raw PSBT. For an incoming
plain PSBT, derivation hints suggest BIP44/49/84/86 accounts. The application builds
the exact default policies from the seed and verifies script matches; it can also
match the session's approved multisig wallet. Hints cannot authorize arbitrary
paths. One match goes directly to review; several matches require selection.
If none can add signatures, complete PSBT account xpubs/origins and input scripts
can provide a supported multisig setup for local approval. This candidate cannot
sign until the user approves it with `REGISTER`. Insufficient setup data asks for
a wallet setup or signer-registration QR; a wrong/missing local key has a separate
message. Invalid or conflicting supplied metadata is rejected rather than repaired.
The result is another `ur:crypto-psbt`, including when only partially signed.

Every input needs valid previous-output data. Legacy/unclassified inputs require
`non_witness_utxo`; supported SegWit inputs may use `witness_utxo`. Missing data
never permits conflicting amounts or scripts. For inputs it signs, Thunder Den accepts only `SIGHASH_ALL` outside Taproot
and `SIGHASH_DEFAULT` for Taproot. Derivation metadata supplies candidate wallet
positions; exact policy-script matches establish ownership and change. See
[Signing](DESIGN.md#signing) for the validation and fee-assurance rules. Fees and
affected totals remain qualified when input amounts are not protected by full
previous transactions or the selected signatures' commitments. Signing those
requests requires the explicit [fee warning](FEES.md), then transaction approval.

This route preserves standard PSBT QR exchange for wallets such as Sparrow using
UR mode. It does not require the command format below. Each pass signs one policy
and preserves existing signatures. [The Sparrow guide](SPARROW.md) explains wallet
setup, compact PSBTs and multiple signing passes. The older preview 4 image used
the stricter full-previous-transaction rule outside all-Taproot spends.
Public descriptor import checks and the scope of physical testing are recorded
in [implementation status](STATUS.md#sparrow-integration).

## Public exports

**Share a public key (xpub)** uses `ur:crypto-account` for the four standard single-key
and three standard multisig shortcuts. Field 1 is the master fingerprint (uint32);
field 2 is a one-element array containing the script-typed public HD key. Single-key
accounts use tags 403 (`pkh`), 400/404 (`sh`/`wpkh`), 404 (`wpkh`) or 409 (`tr`).
Legacy multisig uses tag 400 (`sh`), nested uses 400 then 401 (`sh`/`wsh`), and native
uses 401 (`wsh`). The nested key uses
legacy tag 303, with origin tag 304 and optional coin-info tag 305. It contains
the public key, chain code, complete origin/depth and parent fingerprint. These
are SeedSigner-compatible account-key exports, without a threshold or other
cosigners. The UR type replaces the outer account tag.

The custom-path flow offers either modern `ur:hdkey` or static text such as
`[a1b2c3d4/7h/3/9]tpub...`. Both carry the origin and extended public key, without
an address type. Text preserves Base58 case; public root keys use
`[fingerprint]xpub...`. Neither path exports private keys. See the
[format rationale](DESIGN.md#qr-exchange).

The advanced HD-key QR uses the modern registry: `hdkey` (40303), nested `keypath`
(40304) and `coin-info` (40305). Its outer UR type identifies the object, so the
top-level tag is omitted. No export contains private-key fields.

The user reviews the complete public key and origin, then presses Enter to show
the QR. Public-key exports are not registration instructions. Standalone
descriptor QR export has been removed; complete descriptors remain in policy
Details and incoming multisig setup is supported below.

## Direct multisig setup import

Incoming `ur:crypto-output` uses the legacy registry tags emitted by Sparrow's
Settings descriptor QR: `sh` (400), `wsh` (401), `sortedmulti` (407), `crypto-hdkey`
(303), `crypto-keypath` (304) and `crypto-coin-info` (305).

The supported shapes are `sh(sortedmulti(...))`, `sh(wsh(sortedmulti(...)))` and
`wsh(sortedmulti(...))`. This is a standard-account import: complete origins must
be `m/45h` for Sparrow's legacy layout or `m/48h/coinh/accounth/1h` / `2h` for
nested/native SegWit. Each key must be public with a chain code and consistent
depth/parent metadata. Keys and threshold must satisfy Core's policy checks.

The packet is at most 16 KiB. Definite maps/arrays, minimal integer encodings,
unique known fields and bounded lengths are required. Optional key labels/notes
are bounded to 256 bytes each and ignored. Private keys, bare multisig, unsorted
multisig, explicit child paths and unsupported scripts are rejected. Since Sparrow
omits child paths, `/0/*` receiving and `/1/*` change are explicitly shown and
approved. Mainnet/test-network family is checked; the exact test network remains
the user's local choice.

### Text multisig setup

During the inline setup scan requested by an incomplete multisig PSBT, `ur:bytes`
may instead contain a CBOR byte string holding a public text configuration:

```text
# Multisig setup file
Name: Example
Policy: 2 of 3
Format: P2WSH

Derivation:m/48'/1'/0'/2'
A1B2C3D4:tpub...
Derivation:m/48'/1'/0'/2'
11223344:tpub...
Derivation:m/48'/1'/7'/2'
55667788:tpub...
```

This format supports M-of-N native SegWit sorted multisig with `1 <= M <= N`
and 2 through 20 keys. Each fingerprint/public-key line must have its own preceding
`Derivation:` line with a hardened `m/48h/coinh/accounth/2h` path; apostrophes and
`h` are equivalent. Public keys must use `xpub` on mainnet or `tpub` on test networks.
Origins, key metadata, duplicates and ownership use the same policy validation as
structured setup. Receiving/change branches are `/0/*` and `/1/*`.

The entire CBOR packet is limited to 16 KiB and each text line to 512 bytes. Only
printable ASCII, tabs and LF/CRLF line endings are accepted. Surrounding whitespace,
blank lines and `#` comment lines are ignored. `Name:` is optional, limited to 256
bytes and ignored for wallet identity. Repeated headers, unknown fields, missing
origins, dangling derivations and a key count different from N are rejected.
Only `Format: P2WSH` is supported by this text importer.

The text format is wallet-independent. Its `ur:bytes` payload is interpreted as
wallet setup only during the inline setup scan, not as a Thunder Den command.
At the main menu, `ur:bytes` retains the command format below. Existing
`ur:crypto-output` setup QRs can also be loaded before scanning a transaction.

Successful local registration of either format retains one policy and its proof
in RAM; no reply QR is required. Subsequent plain PSBTs can use that policy.
Existing Thunder Den commands continue to carry their own policy and proof.

## Thunder Den commands

The client sends one complete request and receives one complete reply in
`ur:bytes`. Each request carries its complete arguments; the signer does not ask
for individual policy keys or PSBT fields in follow-up messages. It always uses
its locally selected network and loaded keys.

Every wallet operation carries the complete policy. Signing and address checks
also carry its seed-bound registration proof. The policy-ID and HMAC derivation
have not changed, so saved proofs remain valid for their exact policy text.
Verified BIP44/49/84/86 defaults still use the zero proof. Registration never
replaces transaction approval.

Command signing replies carry raw PSBT bytes and an explicit partial/completed
status. A declined approval returns a correlated refusal. Cancelling before a
request has been decoded returns to the menu without a reply.

Network identifiers are CBOR text strings matching Bitcoin Core's chain names:
`main`, `test`, `testnet4`, `signet` and `regtest`. `test` means legacy testnet3.
Network selection is local and fixed for an application session. Testnet3,
Testnet4 and Signet share address encodings; regtest uses `bcrt1` for SegWit/Taproot
addresses. The network name still identifies the intended chain.

### Encoding and reply matching

Each command message is a deterministic CBOR array wrapped in the CBOR byte string
of `ur:bytes`. Only definite arrays, unsigned integers, byte strings and printable
ASCII text are used. Integers and lengths use their shortest encoding. Field order
and array lengths are exact. Unknown fields, trailing data and incompatible
message formats are rejected. Standard `crypto-psbt` exchange is unchanged.

The inner request is at most 1 MiB + 64 KiB. A reply, including its UR wrapper,
must fit 2 MiB + 64 KiB. Paths contain at most 32 uint32 BIP32 indexes, including
the hardened bit. Flags are unsigned 0 or 1, not CBOR booleans.

The leading `3` below is an internal message-format marker, not a Thunder Den
release number.

```text
request = [3, request_id, network, operation, arguments]
reply   = [3, request_id, network, fingerprint, app_version, operation, status, result]
```

- `request_id`: 16 bytes, unique for every request, including retries. Clients
  start a 128-bit counter at a fresh OS-random value. Never replay approval
  requests automatically after a timeout or disconnect.
- `network`: one of the network identifiers above.
- `fingerprint`: the four raw master-fingerprint bytes, in display order.
  This is standard BIP32 origin information and a useful display/selection label.
  It can collide and must not be treated as proof of ownership.
- `app_version`: the signer's application version as printable ASCII text,
  currently `0.0.1`. GitHub release tags may also include a preview suffix.

The client checks the request ID, operation and network before using a reply.
The request ID catches stale/mismatched QR replies; it is not authentication.
The signer establishes ownership by deriving and comparing the full policy xpubs.
Wallet IDs and seed-bound registration HMACs retain their existing roles and
formats. A fingerprint match alone never authorizes registration or signing.

Clients may cache observed fingerprint/version metadata, but that does not prove
live device availability. Fresh signing and address confirmation always need a
new optical exchange. Saved proofs should be associated with full cosigner key
information rather than a fingerprint alone. No identity handshake is required
before a wallet operation carrying its complete policy and proof.

The QR bridge may answer `GET_INFO` from a successful reply saved for its current
session and the requested network, using the new request's ID. This is remembered
public information, not a fresh reply from the signer. The bridge never reuses
replies for public-key sharing, registration, address confirmation or signing.

### Operations

`wallet` is `[name, template, [key_info, ...]]`. Keep its exact text and key order
when storing it. Limits are 64 name bytes, 8,192 template bytes, 1–32 keys and
512 bytes per key. Existing [policy validation and wallet-ID/HMAC rules](DESIGN.md#wallets)
apply.

| Code | Operation | Arguments | Successful result |
| --- | --- | --- | --- |
| 0 | GET_INFO | `[]` | `[]` (information is in the reply header) |
| 1 | GET_XPUB | `[path, display]` | `[path, xpub]` |
| 2 | REGISTER_WALLET | `[wallet]` | `[wallet_id, proof]` |
| 3 | DISPLAY_ADDRESS | `[wallet, proof, branch, index]` | `[address]` |
| 4 | SIGN_PSBT | `[wallet, proof, psbt]` | `[psbt, added_signatures, complete]` |

`wallet_id` and `proof` are 32-byte strings. `psbt` is raw PSBTv0, at most 1 MiB
on input and 2 MiB on output. Branch is 0 for receive or 1 for change; index is
0–2^31-1. The signer checks policy ownership and proof before deriving the
address or preparing a transaction. Verified default policies use the existing
zero proof. Named policies need registration.

The signer always asks before sharing an xpub, even if `display` is zero.
Registration and signing retain full local review and typed approval. Address
confirmation shows the actual derived address. No host operation accepts seed
words, a passphrase or private keys. No management operation clears or replaces
the local keys.

The returned PSBT is still partial when another signature or preimage is needed.
The client verifies its unsigned transaction against the original before merging
it. `complete` means the signer could finalize a copy, not that chain timelocks
are mature or the transaction was broadcast.

### Errors and cancellation

Status zero means success. Nonzero statuses have result `[]`:

| Status | Meaning |
| --- | --- |
| 1 | User refused |
| 2 | Invalid request, policy, proof or transaction |
| 4 | Local network does not match |
| 5 | Unsupported operation |

Invalid outer headers have no response. Errors contain no input data or exception
strings. The reply always reports the signer's actual local network and master fingerprint.
Status 3 is unused.
Malformed requests must not reach local approval or signing.

Clients must discard late replies to cancelled requests. Cancelling on the online
computer cannot remotely stop offline review; the user can press Esc on the
signer. A new request gets a new ID. Optical capture and local review have no
automatic short USB-style timeout. The signer's no-frame camera timeout is
separate from the time allowed to review a request.

## Bounds and assembly

- Individual QR text: at most 4,296 ASCII characters.
- Message: at most 2 MiB + 64 KiB of CBOR.
- Command request: at most 1 MiB + 64 KiB, excluding its CBOR byte-string wrapper.
- Incoming raw PSBT: at most 1 MiB.
- Returned raw PSBT: at most 2 MiB; transactions have at most 128 inputs and
  128 outputs. Wallet-definition limits are listed in [Design](DESIGN.md#wallets).
- At most 1,024 source fragments; at most `4 * fragment_count + 64` distinct
  sequence numbers per scan. Identical repeated frames do not consume this budget.
- The type, fragment count/size, message length and checksum must stay consistent.
- Conflicting duplicate frames are rejected. A new stream requires a new scan.
- Both individual Bytewords checksums and the completed fountain checksum must pass.
- Lengths and geometry are checked before upstream fountain allocation/processing.

The sender increases fragment size for large messages to stay within the fragment
count bound. Large PSBTs may therefore require denser QR codes. The receiver checks
the CBOR byte-string length using unsigned bounds before iterator arithmetic; it
does not expose the upstream generic CBOR parser directly to scanned lengths.

`tests/transport.cpp` checks published UR vectors, mixed-frame recovery, malformed
lengths/counts, stream conflicts and image encoding/decoding through libqrencode
and ZBar. These are development protocol tests, not physical webcam certification.

## Development runner

`qr-command-runner --alice` and `--bob` run the real command handlers with fixed
public BIP39 test vectors and explicit test approval callbacks. Input and output
are newline-delimited hex of the inner CBOR. `--decline` refuses local approval.
`--fixtures` emits a Core-checked two-signer HTLC workload with a NUMS internal key
and 1/5/10 inputs. `--test` verifies the claim, delayed preimage and refund paths
using Core.

The runner is built only with tests enabled and is never installed in the image.
It is a protocol test tool, not a production mode or a way to load real keys.
