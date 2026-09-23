# Next phase: modern exports and a BHWI QR bridge

**Status: implementation plan.** This document records the agreed direction,
source findings and work needed for the next phase. Proposed protocol details and
performance estimates still need validation. [PROTOCOL.md](PROTOCOL.md) describes
the currently implemented protocol; [STATUS.md](STATUS.md) records verification.

Inspection baselines:

- Thunder Den: `29c6de4`, after the camera lifetime and failure-reporting fixes.
- BHWI fork: `29a9c9c` in
  [landabaso/wizardsardine-bhwi](https://github.com/landabaso/wizardsardine-bhwi).
  Local checkout: `/home/landabaso/bitcoinerlab/wizardsardine-bhwi`.
- Upstream BHWI: [wizardsardine/bhwi](https://github.com/wizardsardine/bhwi).

Recheck these repositories before implementation. In particular, read the BHWI
fork's `AGENTS.md` before changing it.

## 1. Goal and scope

A BHWI client should be able to use Thunder Den through a companion program on
the online computer. The companion displays request QRs and scans response QRs.
Thunder Den remains an offline signer with local keyboard/screen approval.

### Agreed decisions

| Area | Decision |
| --- | --- |
| Public exports | Support `ur:output-descriptor` for full public descriptors and `ur:hdkey` for xpubs. |
| Account bundle | Do not add `ur:account-descriptor`; it overlaps with the selected export operations. Replace the current `crypto-account` export. |
| Wallet model | Keep BIP-388 policies and Bitcoin Core descriptor/Miniscript handling. |
| Registration | Preserve the current Ledger-v2-compatible `Policy::ID()` and Thunder Den HMAC derivation. |
| Exchanges | Send complete, bounded requests and responses. Do not implement Ledger's interactive Merkle-data callbacks. |
| Xpub retrieval | Support wallet-driven `GET_XPUB(path)` so the coordinator selects the correct derivation. Retain direct on-device export. |
| Transport | Use QR/camera between computers. USB device/gadget communication is outside this roadmap. Booting the signer from USB remains supported. |
| Immediate integration | Implement the Thunder Den BHWI backend and companion first, with a demo coordinator for local testing. |
| Liana | Defer Liana integration and PRs until that foundation works locally. |
| Approval | The companion cannot approve on behalf of the user. Recovery input remains on the signer. |
| Simplicity | Prefer complete messages and existing cryptographic/descriptor libraries over new protocols, parsers or abstractions for their own sake. |

The loopback port is on the online computer only. This work does not require
enabling networking or disk storage in the signer image.

### Recommendations that are not yet final decisions

- Use a compact CBOR command envelope with raw PSBT bytes, avoiding base64 growth.
- Start with one active optical exchange at a time.
- Use a small local service with a browser UI for QR display and webcam capture.
  The final UI toolkit and loopback transport are still open.
- Start performance measurements with one, five and ten inputs and two
  independent signing keys.

## 2. Current foundation and gaps

The existing code already provides:

- Seed-derived keys, BIP-388 policy validation, registration proofs and
  approval-gated PSBTv0 signing.
- Standard BIP44/49/84/86 account export, currently encoded as `crypto-account`.
- `REGISTER_WALLET` and `SIGN_PSBT` JSON requests carried in `ur:bytes`.
- UR fragmentation, bounded decoding and an isolated camera/QR worker.
- Session cleanup followed by a fresh signer process without rebooting.

The new phase still needs:

- Modern public descriptor and xpub export encoders.
- A simpler public-export screen; it currently uses the policy-review pager and
  typed `EXPORT` confirmation.
- General public-key requests, including the cosigner paths used by wallet apps.
- Device/session information and policy-bound address verification requests.
- Request/response correlation, typed protocol errors and companion cancellation.
- A BHWI interpreter/transport and the companion program itself.

Keep camera reliability in the acceptance criteria. The baseline removes the
scanner's 60-CPU-second lifetime limit, adds a ten-second no-frame timeout and
avoids repainting an unchanged preview banner. It also provides fixed failure
messages. Physical startup/reconnect reliability still needs validation.

Camera permissions are currently assigned once after network selection. Late or
recreated device nodes can require a new session. This is a known startup lead,
not a confirmed explanation for every HP failure. USB boot detection and the
separate `CONFIG_PM` investigation are outside this phase.

## 3. How public keys, policies and addresses fit together

### Account-level key retrieval

The coordinator chooses a concrete BIP32 origin and requests its xpub. For example,
a test-network native-SegWit multisig key can use:

```text
GET_XPUB m/48'/1'/0'/2'
```

This selects a fixed node in the key tree. The `0'` is an account number, not a
receiving-address index. The signer derives the node from its secret and returns
only its extended public key. Test-network extended public keys use `tpub`.

A bare xpub does not carry its complete origin path or master fingerprint. The
coordinator needs those as well. Our reply should provide enough origin metadata
to assemble a key-information record such as this illustrative placeholder:

```text
[a1b2c3d4/48'/1'/0'/2']tpub...
```

The fingerprint identifies the master key tree, not a physical device model.

### Register a family of addresses

A policy supplies a template and an ordered key-information vector:

```text
template: wsh(sortedmulti(2,@0/**,@1/**))
keys[0]: [a1b2c3d4/48'/1'/0'/2']tpubAlice...
keys[1]: [b1c2d3e4/48'/1'/0'/2']tpubBob...
```

- `@0` selects the first key-information record. It is not an account number,
  address index or a promise that the current device owns that key.
- `/**` means `/<0;1>/*`: receive/change branches followed by an address index.
- The xpub is fixed. The range belongs to the policy's derivation suffix.
- A policy may use other disjoint branch pairs, such as `/<2;3>/*`.

For receive index 17 in this example, each signer uses the child at
`m/48'/1'/0'/2'/0/17`. Change index 8 uses `m/48'/1'/0'/2'/1/8`.
The coordinator derives public children locally from the saved xpubs. No new
xpub request or policy registration is needed for each address.

### Establish ownership

The signer treats the origin and fingerprint as hints. It derives the claimed
origin from its own master key and compares the resulting extended public key
with the supplied one. Fingerprint equality alone does not establish ownership.
Thunder Den already does this in `Policy::OwnedKeys()`.

When signing, concrete PSBT derivations suggest positions within the policy.
The signer still checks that the derived policy script matches the actual input
or change output. It never treats a supplied path as sufficient authorization.

Ledger represents policy keys as key-information strings. BitBox's policy
interface uses structured origin/xpub records. Both separate account-level key
information from the policy's ranges and the later concrete address indexes.

## 4. Preserve policy IDs and registration proofs

The existing scheme remains authoritative:

```text
leaf       = SHA256(0x00 || key_info_string)
branch     = SHA256(0x01 || left_hash || right_hash)

wallet_id  = SHA256(
    version_byte_2 || name_length_byte || name ||
    CompactSize(template_length) || SHA256(template) ||
    CompactSize(key_count) || merkle_root(keys)
)

proof      = HMAC-SHA256(registration_key, wallet_id)
```

Use the existing tree construction in `Policy::ID()`, including the
`std::bit_floor(keys.size() - 1)` split rule for non-power-of-two key counts.
Preserve its vectors and byte-level behavior.
The registration key is derived from the BIP39 seed with the current SLIP-0021
label `Thunder Den wallet policy`, including its NUL prefix in the derivation.

The signer receives the entire policy and computes these hashes locally.
Computing the root does not require optical requests for individual leaves or
proofs. This is independent of Ledger's interactive transport protocol.

After registration, the coordinator stores the exact policy and its proof for
each signer. Later signing and address-verification requests resupply both.
Registration avoids repeating the full policy approval; it does not eliminate
transmission of the policy or transaction approval.

Important invariants:

- The same wallet ID is shared by its cosigners, but different seeds generally
  produce different registration proofs.
- A proof remains valid after a fresh session with the same seed/passphrase and
  exact policy. No policy-ID migration is planned.
- Preserve key order and the exact authenticated representation. Equivalent
  spellings such as `h` versus `'` or `/**` versus `/<0;1>/*` can produce different IDs.
- Do not bind persistent registration proofs to an ephemeral request/session ID.
- Keep the current special handling for verified default policies.
- The host stores an opaque proof; it does not know the HMAC key or grant approval.

## 5. Public export UX

Offer clear, directional actions: **Export descriptor** and **Export xpub**.
Avoid a label such as "Connect wallet app" for a one-way export.

- `ur:output-descriptor` should carry a complete public descriptor, including
  receive/change derivations and its descriptor checksum. Using the format's
  full-text `source` field avoids a second key-placeholder export encoding.
- `ur:hdkey` should carry a public extended key with origin metadata. Its schema
  can represent private keys, but our exporter must only construct public keys.
- Use the current registry schemas and tags: `40308` for `output-descriptor`,
  `40303` for `hdkey` and current tags for nested objects. Renaming a legacy UR
  header alone is insufficient. Validate against published and independent vectors.
- Do not add an `account-descriptor` choice alongside these.
- A complete custom descriptor requires the full wallet definition. A single
  seed does not reveal other cosigners or the wallet's recovery rules.
- Generate exports from the same validated policy/key data used for signing.

Replace the current generic export review with a concise summary of the network,
account/path, address type where applicable and fingerprint. Explain that the
export allows account activity to be viewed and contains no private keys.

**Enter: Show QR** should display the QR directly. Remove the export-only typed
`EXPORT` step and its intermediate "Review complete" screen. Details can show the
full xpub, descriptor, origin and an example receive address. Label that address
as an example so it is not confused with the exported object.

Show page counters only for multiple pages. Show previous/next page controls
only where those pages exist. Distinguish previous-page navigation from leaving
a screen. Public export, policy registration and transaction approval are
different decisions; do not weaken signing or registration approval as a side
effect of simplifying export.

## 6. Architecture and responsibilities

```text
Online computer
  BHWI client / demo coordinator
    -> Thunder Den sans-I/O interpreter
    -> local transport
    -> companion bridge: request QR display + response QR camera
                            |
                      optical exchange
                            |
Offline laptop
  scanner -> validated request -> local review/approval -> response QR
```

### BHWI

- Keep encoding, response validation and protocol state in the core interpreter.
- Use its existing `start`, `exchange` and `end` model. BHWI common methods are an
  API, not a mandatory device wire protocol.
- Keep socket I/O, GUI code and webcam handling outside the core.
- Add a `thunderden` feature, its common-command adapter and explicit
  `DeviceContext::ThunderDen` policy/proof data. Do not reuse Ledger-specific
  context just because the policy-ID algorithm is compatible.
- Reuse `bhwi/src/policy.rs` for template/key extraction and repeated-key handling.
- Honor `GetXpub { path, display }`, including fresh local display when requested.
  Map a successful registration proof to
  `WalletRegistration::Complete { hmac: Some(...) }`; the existing common API
  already supports the 32-byte proof.
- Put endpoint discovery and device inventory in the client/async layers, not
  `bhwi/src/common.rs`.
- Expose a Thunder Den QR device identity rather than presenting it as a Ledger
  or an emulator.

### Companion

The companion relays bounded messages between a local client and the optical
channel. It handles QR display, response assembly, progress and cancellation.
It is part of the untrusted coordinator and cannot approve on the device.

Bind its service to loopback. Specify local access control before implementation;
if a browser UI is used, include origin checks and a per-instance access token.
Keep request data out of URLs and redact sensitive wallet data in logs.

Start with one active optical operation. Metadata queries should not trigger
repeated camera interactions. Cached information describes a previously observed
key session, not proof that a disconnected laptop is currently powered on.

The coordinator owns the wallet definition and registration proofs. A demo
coordinator using BHWI can store these during this phase; the bridge need not
become a wallet or a policy authority.

## 7. Protocol contract to specify first

Use a new, explicitly versioned request/response envelope without changing the
wallet-ID format. Prefer binary CBOR fields for PSBTs and proofs. Exact field
numbers, carrier type and endpoint framing remain to be specified.

The standalone export types describe public objects, not commands. A request
such as policy registration needs its own envelope carried through UR; do not
interpret an arbitrary descriptor QR as an instruction to register it.

| Operation | Required behavior |
| --- | --- |
| `GET_INFO` | Return protocol/app version, selected network and available key-session information. Map BHWI information/fingerprint queries appropriately. |
| `GET_XPUB` | Accept a bounded concrete origin path and return its public key with origin information. Cover cosigner paths such as BIP48, not only current single-key defaults. |
| `REGISTER_WALLET` | Receive the full named policy, verify ownership, obtain local approval and return the current wallet ID/HMAC proof. |
| `DISPLAY_ADDRESS` | Receive the policy/proof and branch/index, derive the address on the signer and obtain the requested on-device confirmation. |
| `SIGN_PSBT` | Receive a full PSBTv0, policy and proof; review the immutable transaction and return the signed PSBT, including partial results. |

Contract requirements:

1. Bind responses to a request identifier, operation, network and expected key
   identity. Decide the session-binding and request-digest rules explicitly.
2. Reject duplicate fields, unknown versions, malformed lengths and unsupported
   operations. Define typed refusal, mismatch, not-ready, busy and transport errors.
3. Never mark registration complete before receiving the device's proof.
4. Define cancellation before and after request decoding. Cancelling locally
   cannot instantly stop an offline device. Discard late responses to cancelled jobs.
5. Keep approval and fresh address verification interactive. Do not satisfy them
   from cached signatures or an earlier display acknowledgement.
6. Permit cached metadata and previously shared xpubs only where the caller does
   not require fresh on-device display. New hardened paths require the device.
7. Host requests must not carry recovery words, passphrases or private keys.
   They must not remotely clear/reseed the signer. Unsupported management methods
   should fail explicitly. Load keys locally before the public optical exchange.
8. Avoid an extra identity scan when its information can accompany the first
   ordinary response. Finalize this against actual BHWI call order rather than
   assuming clients always request an xpub first.
9. Validate returned PSBTs against the original unsigned transaction and preserve
   unrelated PSBT metadata and other signers' signatures when merging.
10. Retain the signer's policy, message and PSBT bounds. Do not remove required
    previous-transaction data merely to shorten an animation.

The current limits include a 1 MiB incoming PSBT, a 2 MiB signed result, 128 inputs
and outputs, 32 policy keys and 1,024 UR source fragments. Audit how the new
envelope fits those limits. Base64-wrapping a maximum-sized signed PSBT would
exceed the existing UR message budget, which is another reason to use binary fields.

The camera's ten-second no-frame watchdog is not an overall request/approval
deadline. A user may spend much longer reviewing a policy. The companion should
wait asynchronously and offer cancellation rather than inherit short USB timeouts
or automatically repeat an operation with uncertain delivery.

## 8. UX and optical-transfer estimates

**These are analytical planning estimates, not generated/validated transaction
fixtures or measured HP performance.** Replace them with benchmark results before
claiming interoperability or a particular completion time.

### Example workload

- Two independent signing keys.
- A 2-of-2 hashlocked tapscript claim and a one-signature timelocked refund.
- A known unspendable internal key, with no key-path bypass.
- One Taproot input and two outputs: payment and change.
- Both tapleaves, their control blocks, derivations and a 32-byte preimage included.
- Ordinary tapscript signatures, not MuSig2.

The illustrative template is:

```text
tr(@0/**,{and_v(v:multi_a(2,@1/**,@2/**),sha256(H)),and_v(v:pk(@1/<2;3>/*),older(144))})
```

`H` stands for a 64-hex-character SHA-256 hash, not a literal template token.
`@0` is the unspendable internal key; `@1` and `@2` are the actual signers.
The refund uses `@1/<2;3>/*` to keep its derived keys distinct from the claim.
The size model used key-string length placeholders, not cryptographically valid
keys; the real benchmark must generate and validate the complete policy and PSBT.

A normal two-party HTLC often needs only one signature for each alternative
spending branch. The 2-of-2 claim is a deliberately heavier example. An internal
key with a spendable key path would change the security meaning of the example;
verify this when constructing the real fixture.

The byte budget used 109-byte and 38-byte scripts, 65-byte control blocks,
BIP48 origins and no optional global xpub records. It gave:

| Item | Modeled size |
| --- | ---: |
| Policy template | 151 bytes |
| Three key-info strings, including the internal key | 379 bytes |
| Unsigned PSBT | 1,480 bytes |
| PSBT after one signature | 1,611 bytes |
| PSBT after two signatures | 1,742 bytes |

A 64-byte tapscript signature occupies approximately 131 bytes as a PSBT entry
once its public-key/leaf-hash key and serialization overhead are included.
Actual coordinators may supply different metadata or additional signatures.

### Scan sessions and frames are different

One scan session transfers one logical message in one direction. An animated
message is collected automatically; the user does not manually scan each frame.

Current defaults are about 200 binary payload bytes per source fragment and a
nominal four displayed frames per second. Rendering adds overhead. Bytewords
expands the data into QR text, so 200 bytes does not mean 200 visible characters.
Missing frames and fountain recovery can require more displayed frames than the
number of source fragments. Approximately 1.5–3 times ideal airtime is a useful
initial allowance, not a guaranteed bound; aiming and startup take additional time.

The following conservative baseline includes a separate session-identity QR and
a proposed CBOR RPC envelope with request/session metadata:

| Transfer | Modeled payload | Source frames |
| --- | ---: | ---: |
| Session identity: signer to bridge | 100–200 B | 1 |
| Xpub request: bridge to signer | 120 B | 1 |
| Xpub reply: signer to bridge | 260 B | 2 |
| Registration request | 678 B | 4 |
| Registration proof reply | 232 B | 2 |
| First signing request | 2,200 B | 11 |
| Partially signed PSBT reply | 1,772 B | 9 |
| Second signer's signing request | 2,331 B | 12 |
| Second signer's PSBT reply | 1,903 B | 10 |
| Separate address-verification request | 733 B | 4 |
| Address-verification reply | 220 B | 2 |

For one fresh signer, through one partial signature, the first seven rows total
**7 scan sessions and approximately 30 source frames**. This assumes the other
party's xpub is already available. With two completely fresh signers, collect both
xpubs before registration; registering and signing on both totals approximately
**14 scan sessions and 62 source frames**.

These are budgeting baselines, not a requirement to add a dedicated identity QR.
Piggybacking identity on the first requested-xpub reply could reduce the automatic
flow to **6 scans per fresh signer**. Manual on-device xpub selection/export can
reduce it to **5**, but makes the user responsible for choosing the matching path.
Prefer automatic path selection and measure the actual onboarding sequence.

Once the key information and proof are saved, signing is normally **2 scans per
signer**. A new key session may require an additional identification exchange,
depending on the chosen bootstrap design. A new address index does not require
re-registration. If one loaded seed controls multiple required keys, one signing
pass can add several signatures.

### Human time budget

For the one-input, one-signer first-use example:

| Activity | Initial planning allowance |
| --- | ---: |
| Seven scans, positioning and ordinary navigation | 30–70 seconds |
| Policy registration review | 30–90 seconds |
| Transaction review/approval | 15–60 seconds |
| Recovery-word/passphrase entry | 1–3 minutes |

Budget roughly **2–7 minutes** for first setup through one signature. Two fresh
independent signers can take roughly **5–14 minutes**. Seed entry and review may
be prepared separately or in parallel. Current verbose review screens can take
longer. Repeated small spends with keys already loaded should target roughly
**30–90 seconds per signer**, including review, subject to measurement.

These estimates exclude funding confirmations and waiting for the HTLC timeout.
If Thunder Den also signs a funding transaction, count another signing round trip.
A separate address check adds two scans; comparing the first receive address
during registration can avoid that extra interaction when appropriate.

Pre-signing without a preimage is a partial-PSBT case to test. Supplying the
preimage later ordinarily does not require new signatures if the transaction is
unchanged. Finalization and broadcasting happen online. Distinguish "signature
added" from "transaction complete" and from onchain timelock maturity.

### Scaling and practical limits

For the same illustrative policy and first signing pass:

| Inputs | Request frames | Reply frames | Ideal combined airtime at 4 fps |
| --- | ---: | ---: | ---: |
| 1 | 11 | 9 | 5 seconds |
| 5 | 28 | 29 | 14 seconds |
| 10 | 50 | 53 | 26 seconds |

Ten inputs might require roughly 40–90 seconds of capture before review. A
100 kB message at 200 bytes per frame needs about 500 source frames: over two
minutes of ideal airtime in one direction. Very large batches can have poor UX.
Above the default fragment-count range, the current sender increases fragment
size, making the QR denser; the table is not a throughput promise for those cases.

All-Taproot spends avoid the need for full previous transactions. Non-Taproot
spends can be much larger, even with few inputs. The signer's fee-verification
requirements remain authoritative.

Small HTLC/vault transactions are a plausible target. Frequent signing, large
batches and time-sensitive multi-party workflows need honest performance testing.
Policy complexity is also a review problem: a short animation does not make a
complex Miniscript policy understandable. Do not substitute a coordinator-provided
English description for verification of the actual policy and keys.

## 9. Implementation milestones

Work in small, reviewable increments. Each milestone has an observable completion
condition; do not mark planned work as implemented in the status documentation.

### A. Protocol and reproducible workload

- [ ] Pin the Thunder Den and BHWI revisions used for development.
- [ ] Generate a valid Core-checked HTLC fixture with a known unspendable internal
      key, two independent signers and both claim/refund paths.
- [ ] Record actual policy/PSBT sizes and encoded source-frame counts for one,
      five and ten inputs, before and after partial signatures.
- [ ] Specify the RPC envelope, bootstrap, error/cancellation rules and byte limits.
- [ ] Add shared C++/Rust vectors proving unchanged wallet IDs and HMAC behavior.

**Done when:** the protocol can be implemented independently from its specification
and the workload figures are generated rather than hand-estimated.

### B. Signer operations and public export UX

- [ ] Implement public-only `output-descriptor` and `hdkey` exports.
- [ ] Replace the export ceremony with a concise summary, Enter-to-show and
      meaningful detail/pagination controls.
- [ ] Add information, general xpub and policy-bound address-verification handlers.
- [ ] Extend registration/signing with correlated responses and explicit errors.
- [ ] Keep local approval, ownership checks and transaction immutability intact.
- [ ] Add a test-only protocol runner using synthetic keys and explicit test
      approval callbacks. Exclude it from the installed image; do not add a
      production auto-approval mode.

**Done when:** export vectors round-trip, receive/change addresses match, refusal
produces no signing and the new handlers pass deterministic protocol tests.

### C. BHWI backend and demo coordinator

- [ ] Add the device feature, interpreter, common adapter and explicit context.
- [ ] Add local transport/client selection without putting I/O in the interpreter.
- [ ] Map information, xpub, registration, address and signing operations end to end.
- [ ] Return explicit unsupported errors for unrelated management commands.
- [ ] Provide a demo coordinator that stores public wallet definitions and
      per-signer proofs and can merge partial PSBT signatures.

**Done when:** a BHWI client completes registration, address verification and
partial signing against the real C++ handlers through the test runner.

### D. Companion QR UI

- [ ] Select the UI/runtime and specify loopback access control.
- [ ] Implement session identification and metadata/xpub caching rules.
- [ ] Display request animations and scan matching responses with visible progress.
- [ ] Handle busy state, cancellation, stale replies and key-session changes.
- [ ] Do not claim remote receipt/approval before an actual response establishes it.
- [ ] Arrange recovery input before public-response scanning; do not put recovery
      words in the online webcam's view or accept them through the bridge API.

**Done when:** the same BHWI operations work through actual encoded QR images
and the operator can recover cleanly from cancellation or a failed scan.

### E. End-to-end correctness and usability

- [ ] Complete a regtest funding/spending flow with partial signatures from two
      independent signers, merge/finalize it and have Bitcoin Core verify it.
- [ ] Exercise primary and recovery paths, missing preimages, valid timelock
      sequences and onchain maturity. The offline signer cannot know chain state.
- [ ] Verify receiving and change addresses at multiple indexes.
- [ ] Reuse proofs after restarting with the same seed/passphrase; reject changed
      policies, wrong seeds, wrong networks and stale/mismatched responses.
- [ ] Test fragmented, repeated, missing and malformed frames and delayed replies.
- [ ] Measure real scan success rates, median/slow-case capture times, review time
      and screen/camera repositioning on the HP and the chosen online platform.
- [ ] Record failures before the first preview separately from QR decode failures.
- [ ] Review whether the policy, partial-signing result and next action are clear
      to a hardware-wallet user.

**Done when:** the complete local BHWI/bridge workflow is demonstrably usable and
correct within documented limits. Only then prepare upstream discussions/PRs.

## 10. Validation and development discipline

For Thunder Den, run the relevant native suites as changes land, then the complete
Docker test command before handoff. Extend protocol, export, transaction and
terminal coverage as needed. Use real encode/decode tests in addition to mocked
transport tests. Tests use public fixtures and regtest funds.

The BHWI fork requires the following non-emulator checks for code changes:

```sh
cargo fmt --all --check
cargo clippy --all --all-features --all-targets -- -A dead_code -D warnings
cargo test --verbose --no-default-features
cargo test --verbose --color always -- --nocapture
cargo test --all --exclude "bhwi-e2e-*" --verbose --color always -- --nocapture
```

Add matching Thunder Den protocol/CLI e2e coverage. Follow the fork's current
rules on feature branches, signed commits and handoff reporting. Commit only
when requested. Do not notify upstream or open PRs before local validation and
user approval.

Keep GUI dependencies and test tools outside the signer image. Announce lengthy
builds and use existing native caches where appropriate. A Docker **test** build
does not update `thunderden.img`: before physical validation, rebuild the standard
**image** target, export its image/checksum/inventory and record the source commit
and verified hash. See [BUILD.md](BUILD.md).

## 11. Deferred integration and open choices

### Liana findings to retain

At inspection, upstream Liana depended on `async-hwi = 0.0.32`, while this BHWI
fork is the newer sans-I/O implementation. Adding a BHWI backend alone will not
make it appear in that Liana version. Recheck the target revision when this work
resumes and plan its device-kind, discovery, policy-context and proof-storage path.

Liana's existing device interface uses `Send`/`Sync`; BHWI's async API deliberately
allows `?Send`. A native adapter can drive the sans-I/O core with suitable I/O
without tightening BHWI's public async bounds.

Its inspected spend builder constructs PSBTv0 and attempts to include complete
previous transactions for non-Taproot inputs. That is a promising match for
Thunder Den, but it must be checked with real fixtures and all required metadata.
Liana integration, its migration strategy and its PRs are deferred.

### Settle during milestone A/D

- Exact CBOR schema, UR carrier and loopback framing/endpoint.
- Compatibility boundaries for current standalone PSBT exchange and version-1
  wallet-policy requests when the new bridge protocol is introduced.
- Bootstrap sequence and whether first xpub retrieval can carry all identity data.
- Session-change detection and caching semantics without implying live connectivity.
- Browser versus native companion UI and initial platform/package targets.
- Measured limits of useful QR density, animation rate and transaction size.
- A verifiable policy-review presentation for the chosen HTLC benchmark.

Do not reopen the settled choice to preserve `Policy::ID()` merely to design the
transport. Likewise, a `SIGN_PSBT` RPC must not become a sequence of optical
Merkle-leaf requests or a scan per transaction input.

## Source map and references

- Signer: [application.cpp](../src/application.cpp), [policy.cpp](../src/policy.cpp),
  [keys.cpp](../src/keys.cpp), [transaction.cpp](../src/transaction.cpp),
  [transport.cpp](../src/transport.cpp) and [main.cpp](../src/main.cpp).
- Current checks: [application tests](../tests/application.cpp),
  [transaction tests](../tests/transactions.cpp), [transport tests](../tests/transport.cpp)
  and [export compatibility](../tests/export_compat.py).
- BHWI fork: `bhwi/src/common.rs`, `bhwi/src/policy.rs`,
  `bhwi/src/common/adapters/`, `bhwi-async/src/lib.rs`,
  `bhwi-async/src/transport/`, `bhwi-cli/` and `docs/DEVICE_ONBOARDING.md`.
- [BIP-388 wallet policies](https://github.com/bitcoin/bips/blob/master/bip-0388.mediawiki).
- [Modern output-descriptor UR](https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2023-010-output-descriptor.md).
- [Modern hdkey UR](https://github.com/BlockchainCommons/Research/blob/master/papers/bcr-2020-007-hdkey.md).
- [Ledger Bitcoin protocol](https://github.com/LedgerHQ/app-bitcoin-new/blob/develop/doc/bitcoin.md).
- Deferred Liana references: [dependencies](https://github.com/wizardsardine/liana/blob/master/Cargo.toml),
  [hardware integration](https://github.com/wizardsardine/liana/blob/master/liana-gui/src/hw.rs)
  and [PSBT construction](https://github.com/wizardsardine/liana/blob/master/liana/src/spend.rs).
