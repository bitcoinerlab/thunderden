# Implementation status

V2 is under development. The development image builds and passes emulated
BIOS/UEFI boot and display checks. Physical-hardware and release verification remain.

## Architecture

Native code links pinned, unmodified Bitcoin Core libraries for keys, descriptors,
Miniscript, PSBT review, and signing. Recovery input uses English words and printable
ASCII passphrases. OpenSSL libcrypto provides PBKDF2 and constant-time comparison.

The signing path runs in-process without a daemon, RPC server, or node database.
Core's RNG, allocation, logging support, and shared MuSig helpers remain linked
dependencies. MuSig policies and input metadata are rejected. The current stripped
Buildroot application is 2,562,192 bytes, with shared libraries installed separately.
The hybrid disk image is 64 MiB.

## Milestones

- [x] Verify reuse of Core BIP32/key code without its node/wallet engines.
- [x] Implement English/ASCII BIP39 and in-memory key sessions as a native library.
- [x] Implement native policy validation, wallet IDs, and HMAC authorization.
- [x] Implement immutable native transaction review and approval-gated signing.
- [x] Implement the local interface and UR v2 transport.
- [x] Integrate the kernel, bootloader, and hybrid image with deterministic disk metadata.
- [x] Verify BIOS/UEFI boot, local account review, and framebuffer QR export in QEMU.
- [ ] Compare independent clean builds of the complete image.
- [ ] Validate physical webcams and supported laptop hardware.
- [ ] Isolate untrusted decoding from keys/approval and enforce least-privilege execution.

`platform/` defines the Buildroot application package, runtime launch, kernel,
and bootloader configuration. `build/image.py` assembles the hybrid disk image.

## Verified library behavior

`docker compose run --build --rm test` passes eight suites on Linux/amd64: `foundation`,
`transactions`, `core-keys`, `native-runtime`, `transport`, `application`,
`export-compat`, and `terminal`.

- BIP39 seed vectors for all five standard word counts, ASCII rejection, and
  exact preservation of passphrase spaces and case.
- Core's published BIP32 vectors, leading zeros, depth limits, and injected
  invalid-child conditions.
- Default-account authorization and registered-policy HMAC verification.
- Wrong seeds, altered names/cosigners, invalid points, duplicate/unused keys,
  overlapping derivations, literal-key injection, and malformed policies.
- Wallet-ID vectors with one, two, and three keys and a CompactSize boundary.
- Descriptor/Miniscript parsing and public script derivation through Core.
- BIP44/49/84/86 signing, custom branches, repeated account-key references,
  legacy/wrapped/native multisig, and SegWit/Taproot Miniscript.
- Timelock and hashlock scripts, missing-preimage partial signing, successive
  cosigners, and final signature/script verification through Core.
- Immutable review and explicit approval before ECDSA/Schnorr signing; rejection,
  missing approval callbacks, and changed seed/network prevent signing.
- False change hints, mismatched scripts/Taproot commitments, incorrect previous
  outputs, duplicate inputs, amount limits, malformed PSBTs/signatures, and
  unsupported signing rules.
- External input disclosure, full-previous-transaction fee accounting,
  witness-only all-Taproot signing, and untrusted-signature-independent size estimates.
- Verified Core source remains unchanged by configuration/build. Selected
  node/RPC/wallet/LevelDB symbols are absent from the signing test executable.
- Signing tests observe only local netlink socket use and no filesystem-write
  opens; they also pass with socket creation forced to fail.
- Published UR v2 vectors, reordered/missing-frame recovery, malformed lengths,
  stream conflicts, duplicate-frame bounds, and BBQR rejection.
- QR generation and recognition through independent libraries, using rotated,
  low-contrast synthetic images.
- Strict wallet-policy request schemas and registration approval bound to the
  original wallet ID even if the caller replaces its policy during the callback.
- Full review traversal and typed consent through a pseudo-terminal, including
  buffered-input rejection, cancellation, resize detection, and masked ASCII entry.
- The actual application retains one seed session across operations and recovers
  to its menu when framebuffer display is unavailable.
- Eight main/test-network account exports decoded/re-encoded by the independent
  `urtypes` codec with matching xpubs, origins, fingerprints, and script types.
- Application, transport, export compatibility, and terminal tests pass with
  AddressSanitizer and UndefinedBehaviorSanitizer; leak detection is disabled for
  this check. The QR/camera shared libraries in that run are not instrumented.

## Verified image behavior

- The Buildroot checksum announcement is signature-verified against the pinned
  release key; the archive hash matches the signed announcement and `.env`.
- Resolved kernel configuration disables networking, block storage, modules,
  swap, core dumps, persistent crash storage, and the checked raw-memory interfaces.
- Packed-initramfs inventory includes file hashes, modes, ownership, symlinks,
  and device nodes. Development executables and GUI-framework libraries are absent
  from the checked installed tree.
- Reassembling the same boot payloads with different file timestamps and time zones
  produces identical disk-image bytes. This is an assembly check, not yet an
  independent clean toolchain/kernel/application rebuild comparison.
- SeaBIOS and 64-bit OVMF boot the image with a generic QEMU x86-64 CPU. The test
  enters public recovery words through the local UI, approves an account export,
  and verifies the framebuffer QR against the expected BIP84 regtest account.

Transaction fixtures use synthetic previous transactions and Core script
verification, not chain/mempool acceptance. Dependency/syscall checks run the
development executables in the restricted Docker container; final-image kernel
restrictions and hardware behavior require separate verification.

The application implements seed entry, policy registration, transaction rendering,
account export, webcam capture/preview, and animated QR output. Physical webcam
tests and independent clean image rebuilds remain release-verification work.
Synthetic-image, pseudo-terminal, and emulated-display checks do not establish
physical laptop/camera compatibility.
