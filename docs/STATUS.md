# Implementation status

As of 2026-09-29, the signer, QR bridge and regular desktop Liana integration are
implemented. Thunder Den is usable with [our Liana fork](https://github.com/bitcoinerlab/wizardsardine-liana)
using Bitcoin Core or Electrum. The Liana integration has not yet been submitted
upstream. The [Sparrow single-signature BIP44 workflow](SPARROW.md) has also been
user-tested on a physical computer.

The QR bridge is published on npm as version `0.1.2`. The latest USB image is
published as the development preview
[`v0.0.1-preview.3`](https://github.com/bitcoinerlab/thunderden/releases/tag/v0.0.1-preview.3),
with its checksum and installed-file inventory. BIOS/UEFI boot checks and USB
write/read-back verification passed. Broader laptop/camera coverage and final
release verification remain outstanding.
Current executable sizes refer to the latest recorded build below.

The [Thunder Den QR protocol](PROTOCOL.md) covers standard PSBT exchange, public
exports and wallet-independent commands. Physical camera measurements remain
separate from the automated checks below.

## Architecture

Native code links pinned, unmodified Bitcoin Core libraries for keys, descriptors,
Miniscript, PSBT review and signing. Recovery input uses English words and printable
ASCII passphrases. OpenSSL libcrypto provides PBKDF2 and constant-time comparison.

The signing path runs in-process without a daemon, RPC server or node database.
Core's RNG, allocation, logging support and shared MuSig helpers remain linked
dependencies. MuSig policies and input metadata are rejected. Camera/QR decoding
runs in an isolated process, separate from keys and local approval. The stripped
Buildroot executables are a 2,566,384-byte signer and an 88,176-byte scanner, with
shared libraries installed separately. The hybrid disk image is 64 MiB. See
[Scanner isolation](ISOLATION.md) for the threat model and enforced boundary.

## Milestones

- [x] Verify reuse of Core BIP32/key code without its node/wallet engines.
- [x] Implement English/ASCII BIP39 and in-memory key sessions as a native library.
- [x] Implement native policy validation, wallet IDs and HMAC authorization.
- [x] Implement immutable native transaction review and approval-gated signing.
- [x] Implement the local interface and UR v2 transport.
- [x] Integrate the kernel, bootloader and hybrid image with deterministic disk metadata.
- [x] Verify BIOS/UEFI boot, local account review and framebuffer QR export in QEMU.
- [x] Isolate the scanner from keys and local approval; verify privilege restrictions.
- [x] Compare complete clean builds using separate rebuilt builders and volumes.
- [x] Record a matching image hash from an Apple Silicon build.
- [x] Publish the browser QR bridge on npm.
- [x] Integrate Thunder Den into a Liana fork for Bitcoin Core and Electrum wallets.
- [x] Record a user-tested Sparrow single-signature BIP44 QR workflow.
- [x] Publish a development USB image with its checksum and installed-file inventory.
- [ ] Validate physical webcams and supported laptops, including reconnects and slow cameras.
- [ ] Complete verification of a release image built from the final source revision.

`platform/` defines the Buildroot application package, runtime launch, kernel
and bootloader configuration. `build/image.py` assembles the hybrid disk image.

## Liana integration

The [Liana fork](https://github.com/bitcoinerlab/wizardsardine-liana) includes
Thunder Den support on `master` at commit `85e7681f`. It uses
[our async-hwi fork](https://github.com/bitcoinerlab/wizardsardine-async-hwi), pinned
to commit `dbbd1a7d29ddda504eb62b5f9d750d7c4ef21d34`. The
[QR bridge](https://github.com/bitcoinerlab/thunderden-qr-bridge) is available as
`@bitcoinerlab/thunderden-qr-bridge@0.1.2`, published from commit `59a6aa3`.
The bridge's current Git checkout includes later connection-caching and interface
improvements through commit `1ac684c`; those changes are not yet published on npm.

Regular desktop Liana can retrieve public keys, register wallets, verify addresses
and sign PSBTs through the bridge. Bitcoin Core, including Liana-managed Core,
and Electrum are supported. The integration has not been submitted upstream and
is not included in standard Liana releases.

The fork built successfully against the pinned GitHub dependency with Rust 1.88.
All 35 GUI tests and 148 Connect-library tests passed, along with formatting,
strict Clippy and translation checks. Local interoperability checks covered
registration, address verification and both primary and recovery spending paths
for P2WSH and Taproot Miniscript wallets. Bitcoin Core verified every input in
those synthetic signing fixtures. Bridge/browser checks covered QR exchange,
cancellation and client disconnection using a simulated camera.

Liana Connect support for Thunder Den is disabled. Proposed client-side token
upload and reload handling is present, but needs an agreed and deployed server
API before it can be enabled. Liana Business end-to-end support is separate work.
Physical laptop/camera compatibility and a complete physical Liana signing flow
still need recorded validation.

## Sparrow integration

On 2026-09-29, the user reported a successful physical single-signature
**Legacy (BIP44), account 0** workflow: public descriptor import, receiving and
the Sparrow/Thunder Den QR signing round trip. See the [walkthrough](SPARROW.md).
A Sparrow multisig walkthrough is planned separately.

Sparrow's BIP84/SegWit QR export omits full previous transactions and was rejected
by Thunder Den. The BIP44 route retains that data; the signer's strict fee checks
remain in place. Automated descriptor-import checks below separately cover all
four standard account types in Sparrow 2.3.1 and 2.5.5.

## Verified native behavior

`docker compose run --build --rm test` runs eleven suites on Linux/amd64:
`qr-commands`, `foundation`, `transactions`, `core-keys`, `native-runtime`, `transport`,
`application`, `export-compat`, `terminal`, `camera` and `isolation`.
Actual native confinement tests require Landlock ABI 6 and are reported as skipped
on hosts without it; scanner operation never falls back to an unconfined mode.

The ARM64 development image also compiles successfully, including the application
and test executables. This compilation check ran under emulation on an AMD64 host;
native ARM64 test execution remains unverified.

- BIP39 seed vectors for all five standard word counts, ASCII rejection and
  exact preservation of passphrase spaces and case.
- Numbered recovery-word entry for all five word counts, immediate word validation,
  earlier-word editing and full phrase retry after a failed checksum, before the
  passphrase prompt.
- Empty passphrases accepted with one Enter; non-empty passphrases require matching
  confirmation and can be retried without repeating the recovery words.
- Core's published BIP32 vectors, leading zeros, depth limits and injected
  invalid-child conditions.
- Default-account authorization and registered-policy HMAC verification.
- Wrong seeds, altered names/cosigners, invalid points, duplicate/unused keys,
  overlapping derivations, literal-key injection and malformed policies.
- Wallet-ID vectors with one, two and three keys and a CompactSize boundary.
- Descriptor/Miniscript parsing and public script derivation through Core.
- BIP44/49/84/86 signing, custom branches, repeated account-key references,
  legacy/wrapped/native multisig and SegWit/Taproot Miniscript.
- Timelock and hashlock scripts, missing-preimage partial signing, successive
  cosigners and final signature/script verification through Core.
- Immutable review and explicit approval before ECDSA/Schnorr signing; rejection,
  missing approval callbacks and changed seed/network prevent signing.
- False change hints, mismatched scripts/Taproot commitments, incorrect previous
  outputs, duplicate inputs, amount limits, malformed PSBTs/signatures and
  unsupported signing rules.
- External input disclosure, full-previous-transaction fee accounting,
  witness-only all-Taproot signing and untrusted-signature-independent size estimates.
- Verified Core source remains unchanged by configuration/build. Selected
  node/RPC/wallet/LevelDB symbols are absent from the signing test executable.
- Signing tests observe only local netlink socket use and no filesystem-write
  opens; they also pass with socket creation forced to fail.
- Published UR v2 vectors, reordered/missing-frame recovery, malformed lengths,
  stream conflicts, duplicate-frame bounds and BBQR rejection.
- QR generation and recognition through independent libraries, using rotated,
  low-contrast synthetic images.
- Camera startup with a simulated streaming driver, frame conversion, transient
  read errors, access-denied reporting and disconnection. A mock framebuffer
  verifies that live preview updates do not erase an unchanged controls banner.
  Reliability on physical cameras remains under validation.
- Strict wallet-policy request schemas and registration replies containing the
  reviewed wallet ID and its seed-bound authorization tag.
- Command parsing rejects malformed/truncated/non-canonical requests, wrong
  networks, wrong keys, wrong proofs and missing approval callbacks.
- Thunder Den commands use a request ID and standard fingerprint metadata. A claimed
  matching fingerprint with another xpub is rejected before registration approval.
- The test-only `qr-command-runner` verifies two-signer HTLC claim/refund paths,
  delayed preimages and stable wallet-ID/HMAC vectors with Bitcoin Core.
- Full review traversal and typed consent through a pseudo-terminal, including
  buffered-input rejection, cancellation, resize detection and masked passphrase entry.
- Address checks, registration and signing expose the wallet ID and full public
  descriptor in Details. Registration keeps spending rules and cosigner keys in
  the required review; switching views preserves required pages and typed consent.
- Signing summaries retain full non-change destinations and amounts, fees,
  verified change totals, qualified mixed-input accounting and active lock conditions.
  Signing/registration Details show technical fields without repeating the summary.
- Completed QR results can reopen their text review and redisplay identical reply
  payloads without repeating approval. Enter leaves the QR visible; Esc finishes
  viewing the result. Static and animated framebuffer paths are tested.
- Repeated Esc/Ctrl-C input at network selection and after cancelling an operation
  does not end the session; loaded keys remain usable until explicit logout.
- The actual application retains one seed session across operations and recovers
  to its menu when framebuffer display is unavailable.
- Normal logout displays its key-clear confirmation only after the signer exits.
  A fresh keyless viewer waits for Enter before starting a new signer; a new
  mnemonic, passphrase and network produce a new account. Failed exits remain stopped.
- Eight main/test-network public `hdkey` exports decoded/re-encoded by `urtypes`
  with updated registry tags and matching xpubs, origins and fingerprints. Compact
  `output-descriptor` maps reconstruct the complete reviewed receive/change descriptors.
- Headless checks against the actual Sparrow 2.3.1 and 2.5.5 descriptor import code
  reproduce the null-key-list failure for the old, valid full-text encoding. The
  compact BIP44/49/84/86 exports import on mainnet and regtest; receiving/change
  addresses at indices 0, 1 and 7 match Bitcoin Core for every fixture. These checks
  exercise UR decoding, descriptor import and wallet derivation, not a physical camera.
- Fresh scanner execution with no inherited parent environment/extra descriptors;
  bounded preview/result pipes, stalled-worker cancellation and reaping.
- Healthy preview streams outlive the no-frame deadline; silent workers, partial
  packets and stalled previews time out. Known failure codes map to fixed messages,
  malformed failure records are rejected and confinement adds no lifetime CPU cap.
- Restricted uid/capabilities, denied application-file writes, real JPEG/QR/UR
  decoding under confinement and denied filesystem/parent-process access.
- Recorded AddressSanitizer and UndefinedBehaviorSanitizer coverage includes the
  application, transport, export compatibility and terminal tests. That run used
  disabled leak detection and uninstrumented QR/camera shared libraries.
  Process-confinement/IPC checks have been verified without sanitizers, including
  on the image's kernel and runtime libraries.

## Verified image behavior

- The Buildroot checksum announcement is signature-verified against the pinned
  release key; the archive hash matches the signed announcement and `.env`.
- Resolved kernel configuration disables networking, block storage, modules,
  swap, core dumps, persistent crash storage and the checked raw-memory interfaces.
- Packed-initramfs inventory includes file hashes, modes, ownership, symlinks
  and device nodes. Development executables and GUI-framework libraries are absent
  from the checked installed tree.
- Reassembling the same boot payloads with different file timestamps and time zones
  produces identical disk-image bytes.
- Two complete clean builds recompiled the toolchain, libraries, bootloader, kernel
  and application in separate, uncached Docker builders with fresh source,
  download and output volumes. The full disk image, kernel/initramfs, BIOS/UEFI
  boot payloads, checksum file and installed-file inventory are byte-identical.
  See [Clean-build comparison](BUILD.md#clean-build-comparison) to repeat the check.
- SeaBIOS and 64-bit OVMF boot the image with a generic QEMU x86-64 CPU. The test
  enters public recovery words through the local UI, approves an account export
  and verifies the framebuffer QR against the expected BIP84 regtest account.
- The optional containment suite passes on the image kernel/runtime through a
  separate test-only initramfs overlay. The installed image has ten BusyBox command
  links, root-owned non-writable application paths and no setuid/setgid files.

The clean-build comparison on 2026-09-10 used source commit
`e4c07d45348b94e2035d16f52bf97f9cffea0a9c`, its pinned inputs and
`SOURCE_DATE_EPOCH=1787529600`. Both runs used the same Linux/amd64 host.
The 67,108,864-byte image SHA-256 was:

```text
8d85faeb3a73bf23378691e8cde3c08cf1940057bda427917e13bc4c7faf1ab3
```

Docker selects native AMD64 or ARM64 build tools while Buildroot targets x86-64.
An Apple Silicon build was reported on 2026-09-21 to produce the same image hash.
Local kernel and installed-file checks also pass, with all eight compared image
artifacts byte-identical to the reference build.

The QR-command implementation image built on 2026-09-24, before the command-file
naming cleanup, is also 67,108,864 bytes. Its SHA256 is:

```text
540156e7f1812bd778a38bb3699ab77e00308a5bb1584d743f97d6c49280444b
```

That snapshot contains a 2,537,744-byte signer and an 88,184-byte scanner. Its
installed-file and repeat-assembly checks passed, as did BIOS/UEFI boot and public
descriptor export in QEMU. It includes uncommitted implementation work after
`057d4c6`; it has not been tested on the physical HP during this phase.

The image with the simplified command format rebuilt on 2026-09-24 is
67,108,864 bytes with SHA256:

```text
8db022d60c556f81aa004c5e4cf279df231ec97ffadc94f3c042c55475ccd2cf
```

It includes the uncommitted protocol simplification after `f3677f0`. The checksum,
installed-file checks, repeat assembly and BIOS/UEFI boot/export tests passed.
Physical HP validation remains outstanding.

A console image was built on 2026-09-27 from
`feature/console-refresh`. It includes the UX refinements after `3b6f6a4`, later
committed in `eec50f9`, but predates that commit's network-aware SegWit xpub example.
Its application version is `0.0.1` and its 67,108,864-byte image SHA256 is:

```text
80103269ba8d951a3250c5fc20e9cbac81c6927069a46afbceba567c5a134fda
```

All eleven native suites passed. Coverage includes hint placement and TAB
toggles, complete expanded review before approval, details cancellation,
mainnet-first network selection and process exit before the keyless completion
screen. Secret-input editing, typed consent and resize rejection remain covered.
QEMU boot/export tests passed for BIOS and UEFI at 1280x800, BIOS at 640x480 and
UEFI at 2560x1600. Each run used public test words, corrected an invalid word,
returned from details with `d`, decoded the descriptor QR and exported again
directly from details after holding Escape without re-entering recovery words.
After logout, Enter returned to a pixel-identical fresh network menu.
Kernel/content checks, checksum verification and repeat assembly passed.
The full clean-build comparison and physical-hardware checks have not been
repeated for this snapshot.

On 2026-09-28, the native development build and terminal suite passed with the
updated xpub example, including mainnet and Signet checks.

The first published preview, `v0.0.1-preview.1`, was then built from source commit
`b12fdbf549cb2a04e4db50abdc1463d8e6f0a3fa`. This image includes the updated example
and reports application version `0.0.1`. Its size is 67,108,864 bytes and its
SHA-256 is:

```text
baeaacb17bbd349449814fbee9ad1853a74b5d21ea0c46510bc4d586869728ec
```

Kernel restrictions, installed-file checks, checksum verification and repeat
assembly passed. QEMU BIOS and UEFI tests passed at 1280x800, including QR export,
cancellation and logout/restart. The image was written to a USB drive and all
67,108,864 bytes matched on read-back. The published image, checksum and inventory
were downloaded from GitHub and matched the original build artifacts byte-for-byte.
The full clean-build comparison and physical laptop/camera checks have not been
repeated for this preview.

The second published preview, `v0.0.1-preview.2`, was built on 2026-09-29 from
source commit `975ddeea1899353086c73de7afa5e7c991654431`. It includes the wallet
review/Details changes and reports application version `0.0.1`. Its size is
67,108,864 bytes and its SHA-256 is:

```text
8ccc6ffb7a88c4db3b5571dd415d95a3777aef2f531dcfe67fd5311830082e71
```

All eleven native suites passed. The source files copied into the image build
match the tagged source. Kernel restrictions, installed-file checks, checksum
verification and repeat assembly passed. QEMU BIOS and UEFI tests passed at
1280x800, including public descriptor QR export, cancellation and logout/restart.
The uploaded image, checksum and inventory were downloaded from GitHub and
matched the original build artifacts byte-for-byte. The full clean-build comparison
and physical USB/laptop/camera checks have not been repeated for this preview.

The third published preview, `v0.0.1-preview.3`, uses source commit
`121945741ab3b0a5d1bd95a67f86e0d1612eca74`. The 2026-09-29 build includes the shorter
signing review, separate technical Details, completed-result QR/text navigation
and removal of QR Pause/Resume. It reports application version `0.0.1`. Its size
is 67,108,864 bytes and its SHA-256 is:

```text
03632638fad260507517d52598646dc51648f7bd265a2ead403d38e676f4a334
```

All eleven native suites passed. The source files copied into the image build
match the tagged source. Kernel restrictions, installed-file checks, checksum
verification and repeat assembly passed. QEMU BIOS and UEFI tests passed at
1280x800, including QR/text round trips, extra Enter presses, Finish and
logout/restart. The image was written to a USB drive and all 67,108,864 bytes
matched on read-back. The uploaded image, checksum and inventory were downloaded
from GitHub and matched the original build artifacts byte-for-byte. Physical
laptop/camera checks and the full clean-build comparison have not been repeated
for this preview.

Transaction fixtures use synthetic previous transactions and Core script
verification, not chain/mempool acceptance. Dependency/syscall checks run the
development executables in the restricted Docker container. The image inventory
and guest checks above verify the built kernel configuration and scanner
confinement; physical hardware behavior requires separate validation.

The application implements seed entry, policy registration, transaction rendering,
account export, webcam capture/preview and animated QR output. Broader physical
webcam coverage and release-candidate verification remain outstanding.
Synthetic-image, pseudo-terminal and emulated-display checks do not establish
physical laptop/camera compatibility.
