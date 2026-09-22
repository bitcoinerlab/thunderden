# Implementation status

V2 is under development. The development image builds and passes emulated
BIOS/UEFI boot and display checks. Physical-hardware and release verification remain.
Image sizes and hashes refer to the recorded build below.

## Architecture

Native code links pinned, unmodified Bitcoin Core libraries for keys, descriptors,
Miniscript, PSBT review and signing. Recovery input uses English words and printable
ASCII passphrases. OpenSSL libcrypto provides PBKDF2 and constant-time comparison.

The signing path runs in-process without a daemon, RPC server or node database.
Core's RNG, allocation, logging support and shared MuSig helpers remain linked
dependencies. MuSig policies and input metadata are rejected. Camera/QR decoding
runs in an isolated process, separate from keys and local approval. The stripped
Buildroot executables are a 2,537,616-byte signer and an 88,176-byte scanner, with
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
- [ ] Validate physical webcams and supported laptops, including reconnects and slow cameras.

`platform/` defines the Buildroot application package, runtime launch, kernel
and bootloader configuration. `build/image.py` assembles the hybrid disk image.

## Verified native behavior

`docker compose run --build --rm test` runs ten suites on Linux/amd64:
`foundation`, `transactions`, `core-keys`, `native-runtime`, `transport`,
`application`, `export-compat`, `terminal`, `camera` and `isolation`.
Actual native confinement tests require Landlock ABI 6 and are reported as skipped
on hosts without it; scanner operation never falls back to an unconfined mode.

The ARM64 development image also compiles successfully, including the application
and test executables. This compilation check ran under emulation on an AMD64 host;
native ARM64 test execution remains unverified.

- BIP39 seed vectors for all five standard word counts, ASCII rejection and
  exact preservation of passphrase spaces and case.
- Numbered recovery-word entry for all five word counts, immediate word validation,
  earlier-word editing and checksum correction before the passphrase prompt.
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
  read errors and disconnection. Physical camera capture still needs verification.
- Strict wallet-policy request schemas and registration approval bound to the
  original wallet ID even if the caller replaces its policy during the callback.
- Full review traversal and typed consent through a pseudo-terminal, including
  buffered-input rejection, cancellation, resize detection and masked passphrase entry.
- The actual application retains one seed session across operations and recovers
  to its menu when framebuffer display is unavailable.
- Normal logout displays its key-clear confirmation only after the signer exits.
  The launcher waits for Enter before starting a fresh process; a new mnemonic,
  passphrase and network produce a new account. Failed exits remain stopped.
- Eight main/test-network account exports decoded/re-encoded by the independent
  `urtypes` codec with matching xpubs, origins, fingerprints and script types.
- Fresh scanner execution with no inherited parent environment/extra descriptors;
  bounded preview/result pipes, stalled-worker cancellation and reaping.
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

Transaction fixtures use synthetic previous transactions and Core script
verification, not chain/mempool acceptance. Dependency/syscall checks run the
development executables in the restricted Docker container. The image inventory
and guest checks above verify the built kernel configuration and scanner
confinement; physical hardware behavior requires separate validation.

The application implements seed entry, policy registration, transaction rendering,
account export, webcam capture/preview and animated QR output. Physical webcam
tests and release-candidate verification remain outstanding.
Synthetic-image, pseudo-terminal and emulated-display checks do not establish
physical laptop/camera compatibility.
