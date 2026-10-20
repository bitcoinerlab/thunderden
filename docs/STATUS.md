# Implementation status

As of 2026-10-20, the signer, QR bridge and regular desktop Liana integration are
implemented. Thunder Den is usable with [our Liana fork](https://github.com/bitcoinerlab/wizardsardine-liana)
using Bitcoin Core or Electrum. The Liana integration has not yet been submitted
upstream. The [Sparrow multisig workflow](SPARROW.md) has been user-tested on a real
device and its image approved for release. The earlier BIP44 single-signature
workflow was also user-tested on a physical computer.
The [Keeper Jade QR multisig workflow](KEEPER.md) was user-tested on iOS/testnet4
with the text-setup development image; that test predates the stricter fee policy
below. See the guide for current limitations.

The QR bridge is published on npm as version `0.1.2`. The latest USB image is
published as the development preview
[`v0.0.1-preview.11`](https://github.com/bitcoinerlab/thunderden/releases/tag/v0.0.1-preview.11),
with unsigned/signed images, public signing material, checksums and installed-file
inventory. Verification is revision-scoped below: earlier boot, USB read-back and
clean-build results do not automatically apply to this latest image. Broader
laptop/camera coverage remains outstanding. Executable sizes and verification
records below refer to their named revisions, not automatically to the latest image.
Preview 8 adds recovery-word generation with mandatory Bitcoin Core randomness,
optional camera/coin/dice input and full backup re-entry. It includes the
[seed-generation audit fixes](../audits/FINDINGS.md) and clearer generation screens.

The [Thunder Den QR protocol](PROTOCOL.md) covers standard PSBT exchange, public
exports and wallet-independent commands. Physical camera measurements remain
separate from the automated checks below.

## Preview 11 startup, camera and QR updates (2026-10-20)

The runtime source is `7a642b562235e9e30a6177aa76e3ca8af0abacaf`. The image was
built from byte-identical source files before that commit; release documentation
does not change its runtime/build inputs. Both images are 67,108,864 bytes:

- Unsigned: `e9c9e622ae3c129d85c636b8c2421e432130e78c063caadf61126931fb317242`
- Signed: `122729796b7466a6db455bda984e1de075c21fd95176f989ebe611108897002f`

Animated QR frames are prepared offscreen, then copied into the QR rectangle
without clearing the existing code or captions. The shared static/SeedQR path
securely wipes staging pixels. This is not an atomic, refresh-synchronized swap;
tearing remains possible. **Change network** is offered only before loading keys.
Camera collection and review now finish before choosing coins or dice. Cancelling
capture discards the attempt, with continuing without camera data as the default
and retrying as an explicit choice. Mandatory Core randomness is unchanged.

Image format 2 stores one self-contained EFI payload, shared by BIOS and UEFI.
Legacy BIOS does not authenticate its EFI signature. Current packaging tools
accept only format 2; use matching tagged tools for older releases. Thunder Den's
own code is now explicitly MIT-licensed; dependencies retain their own licenses.

The camera/display, SeedQR and transport suites passed for this runtime, covering
optical decoding, 16/24/32-bit RGB/BGR layouts, padded strides, viewport offsets,
quiet zones, integer scaling, staging-buffer cleansing and review navigation.
The actual unsigned image booted in QEMU TCG at 1024×768. Its animated public-fixture
hdkey export produced 160 consecutive captures over about eight seconds: no blank
or undecodable frames, no caption changes and exact payload reconstruction.
This was an export test, not a new signing walkthrough or browser-emulator test.

The preceding network change passed terminal and application suites; camera reflow
passed five focused terminal/entropy suites at its recorded revision. Those results
retain their original scope. Source-integrity, frozen/compiled-source matching,
installed-content/kernel, repeat-assembly and canonical-layout checks passed.
Signing remained manual and separate from compilation. Public-only packaging
verified the trusted certificate and both detached and attached EFI signatures.

The full native suite and broad VM matrix were not rerun for this final revision.
No new empty-output clean-build comparison, physical confirmation or independent
audit of this exact image is recorded. Earlier evidence retains its original scope;
TD-005 remains open with conditional authenticated-boot mitigation.

## Seed QR sessions (preview 10)

Current source starts with scanning a seed QR, entering recovery words or creating
a new wallet. SeedQR and CompactSeedQR support 12/24 English words, explicit local
confirmation and the existing optional-passphrase flow. The active menu shows
the seed fingerprint below a blank line, retains the existing scan description
and pins any loaded wallet setup while compact menus scroll.

Generated-word backups show Standard SeedQR beside the words by default; F
switches format and Q hides/shows it. Smaller screens can open a separate QR
view without skipping word pages. A second seed scan cannot replace an active
session: the user keeps the session or explicitly ends its process and rescans.
The [seed QR guide](SEED_QR.md) records format ambiguity and secret-handling limits.

Typed and scanned imports now offer an optional **S: Show words/QR** action before
the passphrase. The private view uses the same words, supports format/visibility
controls and returns to the import review with Enter or Esc. It does not trigger
generation's backup-reentry check or retain the phrase in the loaded session.
Manual 15/18/21-word imports have a words-only view. Normal Continue and queued
input do not display the secret backup.

At `6d1d09b4d7a6e39143c2d28feb36823f6dcea34b`, all 20 rebuilt native suites passed
without skips in 203.71 seconds (two parallel test slots). Coverage includes all nine published SeedSigner vectors,
numeric/binary optical round
trips (including NUL/CR/LF), strict seed validation, address precedence, confined
seed IPC, full VGA previews and large request records. Framebuffer tests decode
12/24-word backups at 640×480, 1280×800 and 2560×1600, switch/hide/restore the QR,
check unchanged word pixels and clear the framebuffer on completion/cancellation.
Terminal tests cover local scan confirmation, passphrases, fingerprint spacing
and compact-menu scrolling. Import-view regressions cover typed/scanned seeds,
all manual word counts, queued-Show rejection, default privacy, repeated viewing,
held-Escape isolation, readable Standard/Compact QR output and private-pixel
clearing before returning to review. Launcher tests retain fresh-process session
clearing.

### Preview 10 image and follow-up checks (2026-10-17)

The released runtime is built from `499f9abe99fcfca7b360b85a98d84e0413c7cf84`.
The release tag adds documentation only above that revision; runtime/build inputs
match the archived build. Both images are 67,108,864 bytes:

- Unsigned: `e681941fadd1131fac8c234300ffc3b305e671632fefa8c4ed19208556476500`
- Signed: `b82acb92955809c2d4513d250afcf02b224cfd72bead3afdd4f58c52a8a7313b`

The `6d1d09b` image passed BIOS at 640×480 and unsigned UEFI at 1280×800,
including decoding Standard/Compact backup QRs, hiding/restoring the QR, returning
to the import review, public-account export and fresh-session restart. Secure Boot
was not enforcing in those UEFI checks.

Later UI changes at `a1929ca` passed the SeedQR, terminal and camera/framebuffer
suites without skips. These check the shared header, orange network label,
dividers, footer, centered QR captions and 12/24-word layouts at 640×480,
1024×768, 1280×800, 1366×768 and 2560×1600, including format/visibility controls
and private-pixel clearing. At `499f9ab`, the application suite passed, including a
regression that keeps the standard public-key review on one 80×24 screen while
retaining its privacy explanation. The final image build reused that matching
passing evidence; the full 20-suite run above was not repeated on this revision.

The final image uses the compatible pinned development build cache. Frozen source
matches the compiled application, installed runtime hashes match the inventory,
and kernel/image-content, repeat-assembly and canonical-layout checks passed.
Manual signing remained separate from compilation. Public-only packaging verified
the approved certificate and both detached and attached EFI signatures.

No fresh empty-output build comparison, final-image VM boot matrix or final-image
physical hardware confirmation is recorded for preview 10. Preview 9's signed-boot,
camera, fee-policy, USB read-back and clean-build results below remain valid for
their original revisions; they are not reclassified as pending or claimed for this
new image. Further independent review remains deferred, and TD-005 remains open
with conditional authenticated-boot mitigation.

## Fee verification (preview 9)

Current source rejects insufficiently verified fees in `ReviewedTransaction::Sign`
before approval or signature generation. The former warning override is removed;
the error directs Sparrow users to its setup guide. Single-input SegWit/multisig,
sufficiently binding Taproot signatures and authenticated multi-input requests
remain supported. See [TD-001](../audits/FINDINGS.md#td-001).

Initial fix verification passed all 18 rebuilt native suites without skips in
193.19 seconds. Regressions cover both attack requests, no approval/signing calls,
empty command error replies, blocking interactive errors at 40/80 columns and
successful safe compact signing.

The recommended Sparrow setup is now **2.5.5+ with Krux**. A user-supplied recording
of its normal Show QR output decoded to a two-input PSBT with both valid previous
transactions. On 2026-10-16, the maintainer also confirmed physical validation of
the fee-policy QR signing workflow during the current candidate session; see
[the current scope](#current-release-candidate-2026-10-16).
Keeper v2.6.3 source exports compact PSBTs through the ordinary Jade QR route;
general multi-input SegWit compatibility therefore needs a full-data export route.

This change is not included in preview 8, but is included in the current candidate.
The integration records below retain their original test scope and signing policy.

## Authenticated UEFI boot (preview 9)

The `feature/authenticated-boot` branch provides optional signing of the complete
EFI kernel and its embedded runtime. UEFI no longer loads GRUB. Fixed built-in
arguments and `noinitrd` prevent external boot arguments/filesystems from
replacing authenticated startup. Legacy BIOS retains its separate GRUB path.
The unsigned/signed release pair uses the same runtime; public-only tools verify
the signing certificate and reconstruct the signed disk exactly from the unsigned
image and detached EFI signature. See [the guide](SECURE_BOOT.md).

Production signing keys are generated and used manually by the owner, outside
the build/agent workflow. Automated signing regressions use disposable keys; the
maintainer has also manually signed the current candidate with a locally held
encrypted image-signing key. Tools accessed only its public certificate/signatures.
The maintainer approved that tested certificate as the project's release-signing
certificate; preview 9 includes its public certificate and detached signature.
Firmware trust, legacy BIOS and rollback remain explicit limits under
[TD-005](../audits/FINDINGS.md#td-005).

### Initial implementation verification (2026-10-15)

All 18 native suites passed without skips (193.21 seconds). The first rebuilt
64 MiB image passed content and repeat-assembly checks; BIOS and direct-UEFI
workflow tests decoded the expected public BIP84 account QR and checked session
restart. The signed image also passed that complete workflow under Secure Boot
and legacy BIOS.

Pinned QEMU/OVMF tests with disposable RSA-3072 code-signing certificates passed:
trusted signed boot, unsigned/wrong-key/modified-runtime refusal, hostile command
line isolation, ignored unsigned GRUB configuration and ignored external
`LoadFile2` initramfs. A pre-fix kernel accepted the injected initramfs as a
positive control. Packaging tests passed exact whole-image reconstruction,
timezone-independent outputs, encrypted-key password prompting, user self-signing
and rejection of valid signatures for different runtime bytes. Public test
evidence is retained under `out/authenticated-boot/`; no production key or host
firmware was used. Two builds from separate empty output volumes produced
byte-identical boot payloads, initramfs, installed-file inventory and complete
64 MiB images, also identical to the cached image used in the boot tests. These
builds shared the pinned builder and verified source/download caches, not compiled
outputs; they are not two independently rebuilt Docker environments. The unsigned
image SHA-256 is
`6d7020a03ed8a6ccd6feea58108b505795c086c7a0fb1f63851fbee4bce1be14`.
Attaching the retained public test signature to a fresh build reconstructed the
same signed image exactly. The existing legacy-path guest-isolation probe also
passed with the rebuilt kernel and toolchain, including real JPEG/QR/UR decoding.
This initial lab checkpoint predates the maintainer's physical follow-up below.

### Current release candidate (2026-10-16)

This subsection records preview 9, not the preview 10 image above.

The candidate uses runtime source `e85d560a69eee6d6ec7a37f35cb8928d27ba5fae`, including
the camera assessment progress/stop controls and completion-or-manual-stop policy
cherry-picked from `master`. Both images are 67,108,864 bytes. Their SHA-256 hashes are:

- Unsigned: `1d1b2d0dbfa8b69c587baecd99c9e70342531469939ed567c48fb031878821dc`
- Signed: `ee39a4c03f4ee53783f15ddc227aa5163d4192b606332f4953b9fdca87b38344`

All 19 native suites passed without skips in 255.46 seconds. The source actually
compiled matches the frozen commit. Installed-file/kernel restrictions and
repeat-assembly checks passed, including fixed boot arguments and `noinitrd`.
BIOS and authenticated-UEFI account-QR/session workflows passed. Current-kernel
OVMF regressions passed unsigned/wrong-key/modified-runtime refusal, external
startup-input isolation and the 100-year-certificate/future-clock checks.

The maintainer manually produced a new signature for these runtime bytes. Public
tools verified it and reconstructed the signed disk byte-for-byte. The exact
signed image completed the account-QR/session workflow under an OVMF trust store
enrolled through the actual authenticated import packages; the matching unsigned
image was refused. Each candidate-runtime USB write had a full 64 MiB read-back
comparison and an EFI-file/signature-state check. The image-signing private key was
never accessed by the agent, build or verification containers.

The maintainer enrolled the public trust configuration on an **HP ProBook x360
11 G5 EE, BIOS S96 01.12.00**, then reported signed UEFI boot acceptance and matching
unsigned-image refusal with the instructed unchanged Secure Boot-enabled,
Legacy-disabled settings. This pair provides reported physical enforcement
evidence beyond the initial import screen's Audit Mode message. The maintainer
also confirmed physical camera behaviour and the fee-policy QR signing workflow
during this candidate session. These are user reports, not independently observed
tests; camera formats/geometry, individual reconnect/slow-device cases, wallet
application/version and broadcast outcome were not separately recorded. They do
not establish cross-device compatibility or measured camera entropy.

The candidate was first built using the pinned builder/toolchain cache. A new,
empty output volume then rebuilt the cross-toolchain, kernel and application.
Its `boot.img`, `grub.img`, `rootfs.cpio`, `bzImage`, complete disk image, checksum
and installed-file inventory matched the tested cached build byte-for-byte.
Applying the retained public signature to that fresh unsigned image reconstructed
the same signed image exactly, without access to the private key. This comparison
shared the pinned Docker builder and verified source/download caches; it was not
an independently rebuilt Docker environment. The earlier two-clean-build result
above retains its original source-revision scope.

At the maintainer's request, a fresh independent review of the corrected/current
source is deferred until after preview 9 and planned further improvements.
See [the review plan](../audits/README.md#follow-up-status-2026-10-16). Preview 9
remains a development preview, not a security certification. TD-005 remains open
with conditional mitigation. Laptop-specific ownership/enrollment material stays
outside the repository and is not part of release assets.

## Architecture

Recovery-word generation is included from preview 8, with mandatory system
randomness and optional camera/manual contributions. See [design and use](ENTROPY.md)
and [measured results](ENTROPY_BENCHMARKS.md).

Native code links pinned, unmodified Bitcoin Core libraries for keys, descriptors,
Miniscript, PSBT review and signing. Recovery input uses English words and printable
ASCII passphrases. OpenSSL libcrypto provides PBKDF2 and constant-time comparison.

The signing path runs in-process without a daemon, RPC server or node database.
Core's RNG, allocation, logging support and shared MuSig helpers remain linked
dependencies. MuSig policies and input metadata are rejected. Camera/QR decoding
runs in an isolated process, separate from keys and local approval. The stripped
preview-10 executables are a 2,722,544-byte signer, a 116,848-byte QR scanner,
a 30,808-byte camera entropy worker and a 220,040-byte NIST assessment worker, with
shared libraries installed separately. The hybrid disk image is 64 MiB. See
[Scanner isolation](ISOLATION.md) for the threat model and enforced boundary.

## Milestones

- [x] Verify reuse of Core BIP32/key code without its node/wallet engines.
- [x] Implement English/ASCII BIP39 and in-memory key sessions as a native library.
- [x] Generate 12/24 recovery words with optional supplemental inputs and full backup re-entry.
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
- [x] Record real-device Sparrow multisig testing and release approval.
- [x] Record user-tested physical address verification across all 13 demo cases.
- [x] Record user-tested Keeper Jade QR multisig signing on iOS/testnet4.
- [x] Record user-reported HP signed acceptance/unsigned refusal and camera/fee-policy QR validation.
- [x] Publish a development USB image with its checksum and installed-file inventory.
- [ ] Extend physical laptop/webcam coverage beyond the reported HP, including reconnects and slow cameras.
- [x] Verify release images against the recorded runtime source and pinned build inputs.

`platform/` defines the Buildroot application package, runtime launch, kernel
and bootloader configuration. `build/image.py` assembles the hybrid disk image.

## Liana integration

The [pinned installation guide](LIANA.md) records the source/package combination:
Liana [`85e7681f`](https://github.com/bitcoinerlab/wizardsardine-liana/tree/85e7681fc0aec8ab3fcfb11b47cb5b32d3cac63d),
with async-hwi pinned in both its manifest and lockfile to
[`dbbd1a7d`](https://github.com/bitcoinerlab/wizardsardine-async-hwi/tree/dbbd1a7d29ddda504eb62b5f9d750d7c4ef21d34),
and `@bitcoinerlab/thunderden-qr-bridge@0.1.2`, published from
[`59a6aa3`](https://github.com/bitcoinerlab/thunderden-qr-bridge/tree/59a6aa35cbfc4b97f0ea99d64fae62a33e49e68d).
Later bridge connection-caching and interface improvements through `1ac684c` are
not part of that npm package. The guide includes its archive checksum and locked
build commands; future upgrades require an updated verification record.

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
the Sparrow/Thunder Den QR signing round trip. The historical steps are in the
[preview 4 guide](https://github.com/bitcoinerlab/thunderden/blob/v0.0.1-preview.4/docs/SPARROW.md).

On 2026-10-01, the user approved the `a56f61b` image for release following their
real-device Sparrow multisig test. Its setup uses the SeedSigner-compatible QR
flow documented in the [current guide](SPARROW.md). The device model, physical
test's Sparrow version, threshold and script type were not separately recorded.
The automated three-script-type matrix is documented below.

In preview 4, Sparrow's BIP84/SegWit QR export omitted full previous transactions
and was rejected. BIP44 retained that data. The newer source accepts compact
SegWit inputs with explicit fee-assurance review, as described in [FEES.md](FEES.md).
Automated descriptor-import checks below separately cover all four standard
account types in Sparrow 2.3.1 and 2.5.5.

### Direct multisig source changes

Source changes after `31c541f` add three multisig key shortcuts, a session-only
approved wallet and automatic selection of verified signing accounts. The direct importer accepts Sparrow's
standard legacy P2SH, nested SegWit and native SegWit sorted-multisig setup QRs.
Wallet CBOR is validated in the signer; the scanner and its pipe enforce size/type bounds.
Legacy/unclassified inputs still require full previous transactions. Fee warnings
for compact inputs remain visible through approval and result review.
The first implementation used static `[fingerprint/path]xpub` exports. Standard
shortcuts now use SeedSigner-compatible `crypto-account`; custom paths offer text
or modern `hdkey`. Existing Thunder Den command formats retain their meanings.
The independent `urtypes` codec verifies the account wrapper, script tags, network
and full key origin. Headless Sparrow 2.5.5 checks decoded 30 account-key exports
across the three types and both networks without changing any xpub or origin.

The branch's architectural cleanup groups public wallet codecs in `wallet_qr`
and user workflows in `application`. The transaction layer depends on policies
and keys, not the application/UI. Candidate reviews share one immutable PSBT
snapshot, resolved outputs and signing precomputation. Policy value copies share
immutable parsed descriptors. Regression checks cover source-buffer mutation and
policy replacement while older reviews/copies remain valid.
The eleven native suites and Sparrow checks passed again after cleanup. Public
export and wallet-fixture vectors matched the pre-cleanup build exactly.

Headless checks using the official Sparrow 2.5.5 runtime covered twelve wallet
setups: 2-of-2 and 2-of-3, all three script types, mainnet and regtest. Its actual
SeedSigner, Krux and Specter public-key importers and the conversion used by its
watch-only text scanner preserved each public key, master fingerprint and path.
Sparrow's actual Settings descriptor encoder produced setup QRs which Thunder Den
imported; receiving/change addresses at indices 0, 1, 7 and 1000 matched Core.

For each setup, one-input and two-input PSBTs passed through Sparrow's actual
serializer with its SeedSigner QR-export flags. Thunder Den added signatures in
two cosigner passes, and Sparrow parsed and verified the returned signatures and
recognized completion. Fee-assurance flags matched the full/compact input data.
These fixtures are synthetic and do not establish chain or mempool acceptance.

Native regression checks cover automatic account selection, one-policy passes,
already-signed inputs, all three multisig types and signature preservation. A
two-request fee-attack fixture verifies that the attack remains possible after
accepting the disclosed risk; a false single-input amount instead makes the
signature invalid against the actual previous output. Pseudo-terminal checks
cover setup approval, cancelled replacement, inline setup during signing,
chooser/back navigation and persistent red fee-warning consent.
All eleven native suites passed with
`docker compose --progress quiet run --build --rm test`, including the confined
scanner's wallet-setup transport and rejection of oversized setup messages.

The earlier `a233f87` branch image passed BIOS/UEFI boot and image checks and was
flashed with full read-back verification. The final `a56f61b` image includes the
subsequent updates below, passed the image and boot checks, and was approved
after the user's real-device test. The current guide targets preview 5 and later.

### PSBT-first setup and menu updates

Changes after `a233f87` reconstruct a candidate standard multisig policy when a
PSBT contains complete global xpubs, origins and input scripts. Registration is
still mandatory local approval, but does not require a second QR scan when that
data is sufficient. The confirmation explicitly says REGISTER does not sign the
transaction yet. Missing data falls back to setup QR scanning; conflicting
metadata, scripts and account keys are rejected.

The public-key menu now includes four single-signature shortcuts and the three
multisig types, using script labels rather than BIP numbers. Recommendation hints
were removed. The public-key review includes the full key without
Details or an encoding field. Wrapped option labels and an orange header status
keep navigation and loaded-wallet state usable on narrow screens.

All eleven native suites passed again. The tests cover candidate approval without
signing, cancellation at REGISTER and SIGN, no extra scan for a complete setup,
missing-data fallback, different cosigner account numbers, conflicting global
key metadata, changed scripts and false change. A mixed standard-account/multisig
request keeps its remaining multisig wallet discoverable after the first pass.

Headless Sparrow 2.5.5 checks passed 36 synthetic transaction round trips across
the twelve-wallet matrix, including PSBT-first setup for both cosigners when
global account data was included. Its account decoder also preserved script type,
fingerprint, path and xpub for 38 single-key/multisig account exports on mainnet and
regtest. These software checks complement the user's real-device multisig test.

The subsequent menu simplification removes standalone descriptor export and its
encoder. The main menu now contains Scan, Share a public key and End session.
Sparrow's guide uses one keystore/account-key import flow for single signature
and multisig, with PSBT-first approval as the normal route and the descriptor-scan
alternative last. Approved PSBT-derived wallets use the same orange Loaded wallet
status as setup-QR registration. Existing Liana `GET_XPUB` commands do not rely
on manual descriptor export.
The eleven native suites and the Sparrow account/signing checks passed after this
simplification. The updated boot probe passed on the `a56f61b` image, checking the
public account QR, three-action menu and session restart in both BIOS and UEFI.

## Physical address verification

On 2026-10-15, the user confirmed successful physical testing of all 13 cases in
`/tmp/opencode/thunderden-address-demo/`. These cover receiving/change addresses,
payment-URI destination checks, another account, index 1007, wrong-wallet and
wrong-network cases, native/nested multisig registration and address checks,
single-signature selection with multisig loaded, Taproot and legacy addresses.
This completes [TD-002's pending validation](../audits/FINDINGS.md#td-002--direct-address-verification).
The report does not separately identify hardware models or the flashed revision;
it does not validate the latest fee-policy change or establish general camera
compatibility. The public cases come from `address-check-tests --fixtures`.

## Text multisig setup and Keeper integration

Current source adds a wallet-independent text multisig configuration codec inside
`ur:bytes`, alongside the existing `ur:crypto-output` setup importer. It is accepted
from the main scanner or during inline setup when a multisig PSBT lacks enough account data.
The schema is documented in [the protocol](PROTOCOL.md#text-multisig-setup).
Both formats use the same policy validation, local key ownership check and typed
`REGISTER` approval. The original PSBT remains loaded for separate `SIGN` approval.
The main scanner recognizes text setup headers separately from binary CBOR commands;
recognition never bypasses the full importer or local approval. Preloading a
wallet displays **Loaded wallet** and avoids another setup scan for matching PSBTs.

All eleven native suites passed with
`docker compose --progress quiet run --build --rm test`. New checks cover 1-of-2,
2-of-2, 2-of-3, 3-of-5 and 20-of-20 policies on mainnet and testnet4, matching
structured/text setup identities and receiving/change addresses. Compact
two-input PSBTs without global xpubs are signed in successive cosigner passes;
the completed transactions pass Core's signature/script verification. Animated
registration QR transport, malformed text/CBOR, network/origin mismatches,
duplicate keys, spoofed fingerprints and size limits are also checked.
Pseudo-terminal tests cover both inline formats, cancellation at `REGISTER` and
`SIGN`, successful replacement and preservation of the old wallet after cancelled
or malformed replacement.
Additional checks cover main-scanner format recognition, preservation of command
routing, preloading either setup format, the orange loaded-wallet status and
signing without another setup scan.

On 2026-10-01, the user reported successful multisig signing with Keeper's current
iOS app on testnet4 using the Jade QR profile. The tested development image matches
application source `576453f` and has SHA-256
`84176b3d53cdab4f79f87a36ed0115a234dc2a5782b23534a13f5387fdd193d7`.
Thunder Den's `ur:crypto-account` imported through Jade; the user scanned the PSBT
first, then tapped **Vault details** in Keeper to display the text registration QR
when Thunder Den requested setup. After `REGISTER`, the retained PSBT could be
reviewed and signed. The device model, exact installed Keeper/iOS versions and
broadcast outcome were not separately recorded.

The main-scanner preload addition follows that physical test and has separate
automated coverage; it was not part of the tested development image. See the
[Keeper guide](KEEPER.md) for both workflows. Neither text setup route is present
in the published preview 5 image.

## Verified native behavior

`docker compose run --build --rm test` runs nineteen suites on this branch on Linux/amd64:
`entropy`, `assessment-progress`, `mnemonic-cleanup`, `entropy-camera`, `entropy-capture`,
`entropy-confinement`, `entropy-assessment`,
`address-check`, `qr-commands`, `foundation`, `transactions`, `core-keys`, `native-runtime`, `transport`,
`application`, `export-compat`, `terminal`, `camera` and `isolation`.
The full suite requires Landlock ABI 6 enabled and usable inside the container.
Only `isolation` reports an unsupported ABI as skipped. Entropy confinement,
assessment and their terminal cases fail on unsupported hosts; production workers
never fall back to an unconfined mode.

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
- Plain address/payment-URI checks for standard accounts and the loaded multisig
  wallet: both branches, bounded ranges, exact indices, cancellation/resume and
  unchanged account/policy selection. Tests cover Base58/Bech32 case, network
  handling, hostile URI parameters and confined QR-to-parent delivery.
- BIP44/49/84/86 signing, custom branches, repeated account-key references,
  legacy/wrapped/native multisig and SegWit/Taproot Miniscript.
- Timelock and hashlock scripts, missing-preimage partial signing, successive
  cosigners and final signature/script verification through Core.
- Immutable review and explicit approval before ECDSA/Schnorr signing; rejection,
  missing approval callbacks and changed seed/network prevent signing.
- False change hints, mismatched scripts/Taproot commitments, incorrect previous
  outputs, duplicate inputs, amount limits, malformed PSBTs/signatures and
  unsupported signing rules.
- External input disclosure, authenticated fee accounting and refusal of unsafe
  compact requests,
  compact SegWit/Taproot signing and untrusted-signature-independent size estimates.
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
  each verified change amount/index, change totals, qualified mixed-input accounting
  and active lock conditions. Change indices above 50,000 also expose the full
  address and a recovery warning in mandatory review. Native cases cover the
  threshold, cancellation and spending existing high-index inputs.
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
- Eight main/test-network public HD-key encodings decoded/re-encoded by `urtypes`
  with updated registry tags and matching xpubs, origins and fingerprints. The
   static public-key text retains that same information. Standard account QRs
   are decoded independently to check script type, network and full origins.
- Historical preview 4 checks against the actual Sparrow 2.3.1 and 2.5.5 descriptor import code
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

The fourth published preview, `v0.0.1-preview.4`, uses source commit
`484b73d06e2fc51a2f62084b3b326e16c41212ee`. Its 2026-09-29 image adds the compact
public descriptor export for Sparrow compatibility, with the strict
previous-transaction requirements preserved. The release documents the user-tested
single-signature BIP44/account 0 workflow. It reports application version `0.0.1`.
Its size is 67,108,864 bytes and its SHA-256 is:

```text
12c00869202f06d39167e3b97584d2aca0a36725714363e9bdfc76f417ca9e71
```

All eleven native suites passed. Independent descriptor reconstruction and the
headless Sparrow 2.3.1/2.5.5 import/address checks passed. The source copied into
the image build matches the tagged source. Kernel/content checks, checksum
verification and repeat assembly passed. QEMU BIOS and UEFI tests passed at
1280x800, including compact descriptor QR export and result-review navigation.
This exact image was written to a USB drive and matched byte-for-byte on read-back
before the user's physical BIP44 Sparrow test. The uploaded image, checksum and
inventory were downloaded from GitHub and matched the original artifacts.
Broader laptop/camera coverage and Sparrow multisig validation remain open; the
full clean-build comparison has not been repeated for this image.

The fifth published preview, `v0.0.1-preview.5`, uses an image built on 2026-10-01
from application source commit
`a56f61bbc9e26131d0d67802b57b1ee515f93451`. It includes the three-action menu,
SeedSigner-compatible account-key exports, PSBT-first multisig approval,
automatic account matching and compact SegWit fee-assurance review. The signer
reports application version `0.0.1`. Its size is 67,108,864 bytes and SHA-256 is:

```text
aa372406709cdb920d989179ac6950b295490253344ca1a5b01c0c879355a047
```

All eleven native suites passed. The independent export codec and headless
Sparrow 2.5.5 account/address/signing matrix passed. The source copied into the
build matches the application source revision. Pinned-input, kernel/content,
checksum and repeat-assembly checks passed. BIOS and UEFI boot checks passed at
1280x800, including public account QR decoding, result-review navigation, extra
Enter, held Escape and logout/restart. The exact image was written to USB serial
`55630D9D`, all image bytes matched on read-back, and the drive was safely powered
off. The user approved it for release following their real-device Sparrow test.
The release tag points to `ada3dbc1059b3aff07158e3a9dc5d41d62cad1c6`, which adds
release documentation to the identical application source and build inputs. All
three assets were downloaded from GitHub and matched the original artifacts
byte-for-byte; the downloaded image checksum passed.
Broader laptop/camera coverage and a fresh full clean-build comparison remain open.

The sixth published preview, `v0.0.1-preview.6`, uses an image built on 2026-10-01
from application source commit `b153aaa23e940e856b4714fbdfb8732ab0733ef7`.
It adds wallet-independent text multisig setup inside `ur:bytes` and main-scanner
preloading with the existing registration approval and loaded-wallet indicator.
The signer reports application version `0.0.1`. Its size is 67,108,864 bytes and
SHA-256 is:

```text
295d0277baa8cc9bdd796017f25a9dd684f85698d2ce0c253f6e273f91633d94
```

All eleven native suites passed. The source copied into the image build matches
the application source revision. Pinned-input, kernel/content, checksum and
repeat-assembly checks passed. BIOS and UEFI QEMU boot checks passed at 1280x800,
including public account QR decoding, result-review navigation, extra Enter,
held Escape and logout/restart. Release documentation is added on top of the
application source revision without changing application code or build inputs.
The user's successful Keeper iOS/testnet4 signing report applies to the preceding
inline-setup image recorded above; main-scanner preloading has automated coverage.
The release tag points to `f1ea515c81b2ef98b379e0521dc178fa40274802`. All three
GitHub assets were downloaded and matched the original build artifacts byte-for-byte;
the downloaded image checksum passed and public asset URLs returned HTTP 200.
The release image was written to USB serial `55630D9D`; all 67,108,864 bytes matched
on read-back, and the drive was safely powered off.
The fresh full clean-build comparison and broader physical-device coverage remain open.

The seventh published preview, `v0.0.1-preview.7`, uses an image built on 2026-10-13
from application source commit `b6b9c16406e3476e6b2a0333281a648f02c327e1`.
It adds plain address/payment-URI verification against a selected standard account
or approved multisig wallet, bounded receiving/change searches and clearer review
screens. Mandatory transaction review now discloses each change index and warns
about recovery at unusually high indices. The signer still reports application
version `0.0.1`. Its size is 67,108,864 bytes and SHA-256 is:

```text
4ebcf74e19543399f09db6acc8747b1bacd511a6519bdd64b738f1efca826073
```

All twelve native suites passed, with none skipped. The build/test invocation
reached its tool timeout during the last suite; that isolation suite passed when
rerun separately. Tests cover address/network validation, both address branches,
account selection, bounded search, full-address display, compact-screen guidance,
stop/resume, wallet replacement and approval boundaries. Source and pinned build
inputs match the image build; installed-content/kernel checks, checksum and repeat
assembly passed. BIOS/UEFI boot checks passed at 1280x800, including account QR
decoding, review navigation and session restart. Release documentation is added
above the application revision without changing application code or build inputs.
The image contains no OCR executable, models or OpenCV. Dependencies were reused
from the non-OCR build cache; a fresh full clean-build comparison and physical-camera
validation of the new address screens remain pending. The compact-PSBT fee risk
remains as documented in [FEES.md](FEES.md) and audit finding TD-001.

The eighth published preview, `v0.0.1-preview.8`, uses an image built on 2026-10-14
from application source commit `b0ab76d5cd99722fb478af10a2264f706ae09125`.
It generates 12 or 24 BIP39 words using mandatory Bitcoin Core randomness, with
optional camera, coin or dice contributions. The camera is selected by default;
manual input defaults to continuing without coins or dice. Generated words require
complete backup re-entry before use. Existing 12/15/18/21/24-word import is retained.
The signer still reports application version `0.0.1`. The image is 67,108,864 bytes
and its SHA-256 is:

```text
43f3a7b9bcb5febea564f2f02a4137cb176c73a31392abe98581865094b94d4d
```

All 18 native suites passed, none skipped, in 196.99 seconds. Coverage includes
mandatory RNG failure, supplemental input, worker confinement, camera timing and
format fallback, assessment report validation, backup re-entry, cancellation,
compact-screen guidance and existing signing/QR flows. The compiled source matches
the application revision. Image content, permissions, kernel restrictions, checksum
and repeat-assembly checks passed. BIOS and UEFI QEMU boot checks passed, including
public account QR decoding, review navigation and session restart. The image was
written to USB serial `55630D9D`; the full 64 MiB read-back matched, and the drive was
safely powered off. Release documentation is added above the application revision
without changing application code or build inputs.

The image includes the fixes recorded for TD-006–TD-008 and TD-010–TD-013.
Independent AI audits reviewed the earlier `6b22f6d` revision; a fresh independent
audit of the corrected revision and physical-camera validation remain pending.
Camera statistics describe the checked data, not a guaranteed contribution to the
wallet's randomness. This build reused the pinned toolchain/dependency cache;
a fresh full clean-build comparison remains pending. TD-009 remains open, as does
the compact-PSBT fee risk (TD-001); see [audit findings](../audits/FINDINGS.md).

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
