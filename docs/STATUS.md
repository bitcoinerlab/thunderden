# Implementation status

V2 is under development. There is no bootable v2 release yet.

## Architecture

Native code links pinned, unmodified Bitcoin Core libraries for keys, descriptors,
Miniscript, PSBT review, and signing. Recovery input uses English words and printable
ASCII passphrases. OpenSSL libcrypto provides PBKDF2 and constant-time comparison.

The signing path runs in-process without a daemon, RPC server, or node database.
Core's RNG, allocation, logging support, and shared MuSig helpers remain linked
dependencies. MuSig policies and input metadata are rejected. Production binary
and image sizes await application and platform integration.

## Milestones

- [x] Verify reuse of Core BIP32/key code without its node/wallet engines.
- [x] Implement English/ASCII BIP39 and in-memory key sessions as a native library.
- [x] Implement native policy validation, wallet IDs, and HMAC authorization.
- [x] Implement immutable native transaction review and approval-gated signing.
- [ ] Implement the local interface and QR transport.
- [ ] Integrate the kernel, bootloader, and reproducible disk image.
- [ ] Verify BIOS/UEFI boots and supported physical hardware.

`platform/` contains the kernel and bootloader baselines for image integration.

## Verified library behavior

`docker compose run --build --rm test` runs four passing suites on Linux/amd64:
`foundation`, `transactions`, `core-keys`, and `native-runtime`.

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

Transaction fixtures use synthetic previous transactions and Core script
verification, not chain/mempool acceptance. Dependency/syscall checks run the
development executables in the restricted Docker container; final-image kernel
restrictions and hardware behavior require separate verification.

Interactive seed entry, policy registration approval, transaction rendering,
QR exchange, and the bootable image remain to be implemented. The library callback
tests establish the approval boundary; they do not verify a physical user interface.
