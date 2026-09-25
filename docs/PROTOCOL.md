# Thunder Den QR protocol

Thunder Den defines these message formats independently of any particular wallet
or companion program. Any client implementing them can exchange QR messages with
the signer. There is no network server on the offline laptop.

The application uses UR v2 with Bytewords-minimal encoding and fountain-coded
multipart messages. PSBTs remain version 0. BBQR and legacy multipart formats are
not supported. Outgoing UR text is uppercase for QR alphanumeric encoding.

| Message | UR type | Purpose |
| --- | --- | --- |
| Plain PSBT | `crypto-psbt` | Standard transaction exchange with a locally chosen account |
| Thunder Den command | `bytes` | Information, xpub retrieval, registration, address checks and policy-based signing |
| Public descriptor | `output-descriptor` | One-way export of a complete public wallet descriptor |
| Public extended key | `hdkey` | One-way export of a public key with its origin |

## Human-operated exchange

The user chooses when to scan on Thunder Den, reviews the requested operation and
approves locally when required. The signer then displays the reply QR for the
online client to scan. Animated frames are collected automatically within each
scan. The computers can be repositioned between steps; continuous camera alignment
is not required. Recovery input stays on the signer and should be completed before
the online camera is pointed at its screen.

## Standard PSBT exchange

`ur:crypto-psbt` contains a CBOR byte string holding the raw PSBT. For an incoming
plain PSBT, the user selects a BIP44/49/84/86 account locally. The application builds
the exact default policy from the seed and uses the normal policy/approval engine.
The unsigned request is never allowed to choose a private-key path implicitly.
The result is another `ur:crypto-psbt`, including when only partially signed.

Every input must include its full previous transaction unless all inputs are
Taproot. For inputs it signs, Thunder Den accepts only `SIGHASH_ALL` outside Taproot
and `SIGHASH_DEFAULT` for Taproot. Derivation metadata supplies candidate wallet
positions; exact policy-script matches establish ownership and change. See
[Signing](DESIGN.md#signing) for the validation rules.

This route preserves standard PSBT QR exchange for wallets such as Sparrow using
UR mode. It does not require the command format below. A real Sparrow round trip
still needs verification, including its previous-transaction data and separate
support for importing the public export formats.

## Public exports

**Export descriptor** produces `ur:output-descriptor` with a complete public
receive/change descriptor and checksum in map field 1 (`source`).
**Export xpub** produces public-only `ur:hdkey`, including chain code, origin,
master fingerprint and network information. Public root keys are supported.

These use the current Blockchain Commons registry: `output-descriptor` (40308),
`hdkey` (40303), nested `keypath` (40304) and `coin-info` (40305). The outer UR type
already identifies its CBOR object, so the top-level tag is omitted. Private-key
fields are never emitted. The previous `crypto-account` export has been replaced.

The user sees a short summary with optional details and presses Enter to show the
QR. Public exports are not registration instructions. The scanner accepts only
PSBTs and Thunder Den commands, not arbitrary descriptors or key exports as commands.

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
release number. Thunder Den has not been released.

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
  currently `0.0.1`. This version labels the development build, not a release.

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
