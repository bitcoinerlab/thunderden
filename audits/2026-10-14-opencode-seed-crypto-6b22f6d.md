# Independent review: Thunder Den seed generation

- **Revision:** `6b22f6dfb27371f0fb160e4ca8f068f72cfa335e` (`feature/seed-generation`).
- **Comparison base:** `a008c3bc2a66827ce3bf23ce3f3611229f74a4f6`.
- **Original analysis window:** 2026-10-14T05:52:57Z–2026-10-14T06:09:09Z.
- **Reviewer:** fresh-context OpenCode general-agent reviewer, independent of the implementing conversation. No model/effort configuration is asserted.
- **Method:** source-first review and targeted public-fixture probes. No repository changes, real wallet secrets, physical cameras, disks/USB devices, SSH material, or subagents were used.
- This publication is report preparation only: no additional testing or production changes. The original `REPORT.md` is preserved unchanged.

## 1. Confirmed finding

### CRYPTO-01: Complete mnemonic entropy remains in an uncleansed stack hash context

**Severity: Medium, conditional on memory exposure. Confidence: High.** This is a defense-in-depth defect, not a demonstrated remote exploit.

**Location:** `src/keys.cpp:53`, in `ValidateMnemonic`:

```cpp
CSHA256().Write(bits.data(), entropy_bytes).Finalize(checksum.data());
```

Bitcoin Core 31.1's `CSHA256` has a 64-byte message buffer and no cleansing destructor (`src/crypto/sha256.h:13-27`). For these 16-/32-byte inputs, `Write` copies the complete entropy into that buffer; `Finalize` processes the padded block without erasing it (`src/crypto/sha256.cpp:699-740`). The validator's secure vectors are destroyed, but this ordinary stack context is not cleansed.

The new encoder wipes its first checksum context at `src/keys.cpp:64`, then calls the validator at line 75, reintroducing the complete entropy into uncleansed storage. Backup display, re-entry, and seed derivation invoke validation again (`src/generation.cpp:41`, `src/terminal.cpp:389`, `src/keys.cpp:82`).

**History:** The validator defect exists in the comparison base. It is inherited, not a newly introduced regression; generation adds paths that depend on it.

**Reproduction:** The reviewer's 38-line `residue_probe.cpp` links the native test image's unchanged application/Core libraries. Linker wrappers observe the real `CSHA256::Finalize` and `memory_cleanse`; they do not replace the hash or validator implementation. Public entropy inputs are `40 41 ... 4f` and `40 41 ... 5f`. An immediate bytewise snapshot after validation returns finds the complete entropy:

```text
ENCODE initial_hash_wipe_observed=1
VALIDATE entropy_bytes=16 entire_entropy_in_returned_stack_context=1 hash_cleanse_calls=0
ENCODE initial_hash_wipe_observed=1
VALIDATE entropy_bytes=32 entire_entropy_in_returned_stack_context=1 hash_cleanse_calls=0
```

The encoder wipe is a positive control for the observer. This is a platform-specific forensic read of abandoned stack storage, not portable application code or a measurement of residue persistence. The coordinator subsequently reported independently reproducing the same result using the same probe against unchanged source; that report is separate from this reviewer's original analysis window.

**Disassembly corroboration:** `/build/thunderden-signer` in the **native Docker test image** creates the validator context at stack offset `0x50`, finalizes it, destroys two vectors, and returns without cleansing the context. The relevant observed addresses were `0x20242`–`0x2032e`. This reviewer did **not** freshly disassemble a USB-image signer binary.

**Prerequisites and impact:** An attacker needs a separate way to read residual signer stack memory, a sufficiently timed physical-memory snapshot, or a relevant swap/hibernation exposure. None was demonstrated. `PrepareSigner` restricts normal dumps and unprivileged process-memory access; no bypass was established. Recovered entropy reconstructs the entire phrase. An empty-passphrase wallet is recoverable from it; a nonempty passphrase remains an additional requirement, with offline guessing limited by its strength.

**Recommendation:** Apply reliable scope-bound cleansing to secret-bearing validation hash state, including exception paths, and verify optimized-build behavior. The statement that hash contexts are erased in `docs/ENTROPY.md:91-92` is too broad for this path. No fix was implemented.

**Other confirmed vulnerabilities:** None established. No weak/deterministic generation route, encoding-bias defect, backup bypass, or premature session creation was found in the exercised cases.

## 2. Evidence identity and residue reproduction

The original audit used local image `thunderden-v2-test`, ID:

```text
sha256:81248a4e6f0827ed284b1928f505ae7ee20f32bf7482475fcc5550420ba1ec38
```

It was created at 2026-10-14T05:40:03.973101429Z. Before trusting its libraries, the reviewer compared `CMakeLists.txt` and all regular files immediately under `src/` and `tests/` with image `/work`: **77 matched, none differed or were missing**. The repository was clean at the requested revision, and final verification found the same image ID/revision. Core metadata identified 31.1 and the build enabled `HAVE_GETRANDOM`.

This is source-correspondence evidence, not independent authentication of upstream or USB-image provenance. The pinned English wordlist SHA-256 was `2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda`.

**Planned coordinator archive:** `audits/evidence/seed-6b22f6d/residue_probe.cpp` (the 38-line source), with shared `audits/evidence/seed-6b22f6d/build.py` and `audits/evidence/seed-6b22f6d/README.md`. These are future publication paths, not files committed by this reviewer.

The following path-normalized recipe expresses the original residue build approach after that archive is available. It requires the recorded local image ID; it does not rebuild or alter production source. `$PWD` is the repository root. The explicit compiler invocation avoids assuming an interface for the planned shared `build.py`.

```sh
IMAGE=sha256:81248a4e6f0827ed284b1928f505ae7ee20f32bf7482475fcc5550420ba1ec38
AUDIT_SCRATCH=/tmp/seed-audit-crypto
ls -ld /tmp
mkdir -p "$AUDIT_SCRATCH"
docker run --rm --network none --read-only \
  --user "$(id -u):$(id -g)" --cap-drop ALL \
  --security-opt no-new-privileges:true --ulimit core=0:0 \
  --tmpfs /tmp:rw,nosuid,nodev,noexec,mode=1777 \
  -v "$PWD/audits/evidence/seed-6b22f6d:/evidence:ro" \
  -v "$AUDIT_SCRATCH:/audit" "$IMAGE" sh -c '
  c++ -std=c++20 -Os -fstack-reuse=none -fstack-protector-all \
    -ffunction-sections -fdata-sections \
    -I/work/src -I/opt/bitcoin/src -I/opt/bitcoin/src/secp256k1/include \
    -I/build/core/src -I/build -Wl,--gc-sections \
    /evidence/residue_probe.cpp -o /audit/residue_probe \
    -Wl,--wrap=_ZN7CSHA2568FinalizeEPh -Wl,--wrap=_Z14memory_cleansePvm \
    -Wl,-rpath,/usr/local/lib \
    /build/libthunderden.a /build/core/lib/libbitcoin_common.a \
    /build/core/lib/libbitcoin_consensus.a /build/core/lib/libbitcoin_util.a \
    /build/core/lib/libbitcoin_crypto.a /build/core/lib/libbitcoin_clientversion.a \
    /build/core/src/secp256k1/lib/libsecp256k1.a \
    /build/core/src/univalue/libunivalue.a /usr/lib/x86_64-linux-gnu/libcrypto.so \
    /build/libbc-ur.a /usr/local/lib/libqrencode.so \
    /usr/local/lib/libzbar.so /usr/local/lib/libv4l2.so \
  && /audit/residue_probe'
```

The original final build and probe exited 0 with the four lines above. Execution uses the audit bind mount because container `/tmp` is noexec. This normalized publication recipe was not rerun during report preparation.

## 3. Positive properties established

**BIP39 and passphrases:** `src/keys.cpp:58-95` accepts exactly 16/32 entropy bytes for generation, uses MSB-first 11-bit dictionary indices and the correct 4-/8-bit checksum, and performs PBKDF2-HMAC-SHA512 with 2,048 iterations and salt `"mnemonic" || passphrase`. There is no modulo-based word selection. Printable ASCII passphrases are explicitly limited to 128 bytes, preserve case/spaces, and are NFKD-stable; unsupported Unicode is rejected. The 64-byte PBKDF2 output does not increase mnemonic entropy.

The independent Python reference used binary-string BIP39 encoding, `hashlib`, and the pinned wordlist rather than application decoding routines:

| Independent probe group | Passed cases |
|---|---:|
| BIP39: repeated-byte, walking-bit, and public SHAKE fixtures, both sizes | 2,392 |
| PBKDF2: empty/nonempty, spaces/case, and 128-byte ASCII passphrases | 200 |
| Manual contribution envelopes | 18 |
| Camera contribution envelopes only | 15 |
| Final mixing across lengths and source combinations | 240 |
| Invalid inputs: lengths, tags, alphabets, shapes, phrases, passphrases | 51 |
| **Total** | **2,916** |

**Mixing:** `src/entropy.cpp:8-71` uses ASCII-plus-NUL domain labels, tags, BE64 field lengths, fixed order, and explicit presence/type records. Absent sources differ from present zero digests; retired manual tag 2 is rejected. Word count, camera profile/count, and full manual outcome sequences are committed. Mandatory fresh randomness is a distinct 32-byte field. Twelve-word output truncates to 16 bytes after cleansing the discarded tail.

Under SHA-256 and fresh-RNG assumptions, no demonstrated attack lets an optional predictable contribution cancel the mandatory contribution. This is not an information-theoretic extractor proof or justification for adding entropy estimates across sources.

**Actual Core RNG:** `GenerateMnemonic` (`src/entropy.cpp:74-79`) calls `GetStrongRandBytes`; Core `src/random.cpp:607-610` selects `SLOW` seeding with `always_use_real_rng=true`. `SeedSlow` obtains fresh OS randomness even after initialization. Startup seeding is lazy; missing an explicit `RandomInit()` call is not an initialization defect.

The independent fault probe wrapped **only `getrandom`**, leaving actual Core and generation implementations intact. It deliberately enabled Core's deterministic test mode and tested `EIO`, `EINTR`, zero return, and 31-byte return, each with cold/warm state and absent/present manual input. **All 16 combinations aborted with SIGABRT (`returncode=-6`) after requesting 32 bytes with flags 0; none returned a mnemonic.** Warm cases required a new OS read. Test mode did not bypass strong randomness.

The native test-image signer contained real strong-RNG/generation symbols and none of the entropy wrapper/state symbols or `MakeRandDeterministicDANGEROUS`. Source inspection found no production CLI/environment switch to deterministic generation. This symbol observation does not assert a separate inspection of the USB-image binary.

**Manual input, backup, and session:** Source requires exact UI targets of 150/300 coins or 64/128 dice, alphabet filtering, backspace correction, and explicit completion. Camera cancellation is distinct from an error offering explicit omission/retry. Generation consent precedes word display; exact whole-phrase comparison precedes return from `NewMnemonic` (`src/generation.cpp:69-96`). `Keys` is created afterward, following optional passphrase confirmation (`src/main.cpp:108-127`).

Independent public-fixture PTY testing passed **all six result groups**:

1. Four pre-generation cancellation points, with zero generation RNG calls.
2. Compact 12-/24-word backup: invalid word/checksum, valid wrong phrase, unchanged retry, exact verification.
3. Resizing revealed backup fails closed.
4. 24-word dice: invalid keys ignored, early Enter rejected, correction preserved, exact 128-result target.
5. Actual main object: backup before passphrase/xpub, exact passphrase retry, session reuse, logout; displayed regtest `m/84h/1h/0h` tpub matched independent Python HMAC/secp256k1/BIP32 for `" A Case  "`, not trimmed `"A Case"`.
6. Actual main: cancellation at backup, verification, or passphrase leaves no key session.

The main-object UI probe used link-time public RNG fixtures, not real wallet secrets. Vector/RNG/UI harnesses and logs are retained local audit artifacts; this report does not claim they are committed or installed as ongoing CI regressions.

## 4. Conditional limitations and unresolved questions

- **Locking:** Core cleanses secure allocations on free, but `LockedPoolManager::LockingFailed()` returns true (`Core src/support/lockedpool.cpp:381-385`). With `--ulimit memlock=0:0`, the public allocator probe printed `SECURE_ALLOCATION_SUCCEEDED used=32 total=262144 locked=0`. This confirms fail-open locking, not that the intended appliance swaps secrets. Ordinary stack state is outside the secure allocator.
- **Manual entropy:** The claimed `-n*log2(q)` bounds are approximately 133.35/266.69 bits for coins at conditional maximum probability 0.54, and 128/256 bits for dice at 0.25. Physical behavior and attacker knowledge must satisfy those assumptions. Counts and tests cannot prove them; key repetition or invented outcomes can invalidate them.
- **RNG limitations:** API success is not proof against a silently defective/adversarial RNG. Early-boot `getrandom` and Core's RDSEED startup loop can block; synchronous generation cannot poll Escape inside Core. These are conditional availability/trust limitations, not observed weak-output fallbacks; stalls were not induced.
- **Backup/display:** Re-entry proves agreement, not durable physical backup. Passphrase repetition proves typing agreement, not recoverability. Clear sequences and masked-output tests do not establish physical console/framebuffer or memory erasure.
- **Suspected residues needing validation:** Temporary HMAC/SHA512 state in `src/keys.cpp:100-108` and Core `src/key.cpp:491-497`, OpenSSL internals, compiler spills, and complete exception paths were not exhaustively studied. CRYPTO-01 does not establish the contents/lifetime of those other possible copies.
- **Access and persistence remain unproven:** The demonstrated residue is immediately after validation. Persistence through later menus/logout, practical extraction, and every target terminal copy were not measured. No second exploit is claimed.

## 5. What existing tests establish—and do not

- BIP39 round trips in `tests/entropy.cpp:22-42` reuse the application's word-index routine; fixed reference outputs help, but round trips alone do not exclude correlated errors. This audit added independent encoding/mixing calculations.
- `tests/entropy_rng.h:15` substitutes `_exit(91)` for failure. The UI test proves no fallback after that modeled exit, not actual Core OS-read/abort/refresh behavior. The 16 fault cases above address that gap for this native Linux build.
- Unequal consecutive phrases in `tests/entropy_guest.cpp:33-37` are a useful smoke test; an advancing deterministic PRNG could pass. They do not establish 128/256 bits of unpredictability.
- Existing fixture-based generation tests do not alone establish the actual main program's generated-wallet/passphrase integration. The independent tpub check supplies additional evidence.
- Masked transcript tests do not establish stack cleansing, locked-page availability, or complete erasure. CRYPTO-01 survives ordinary functional/vector tests.

## 6. Scope exclusions, next steps, and conclusion

This reviewer did not rerun the whole native/guest suites or authenticate image provenance; those were assigned elsewhere. Also outside the completed testing: physical entropy quality, camera/NIST/process-confinement internals, device/driver behavior, USB-image disassembly, physical terminal persistence, cold-boot/swap extraction, exploitability of arbitrary memory corruption, exhaustive crypto-library residues, full signing/QR-parser security, or recovery using real holdings.

Next steps: address CRYPTO-01 with optimized-build and exception-path verification; narrow cleanup claims; resolve the separately assigned reviews; verify target locking/no-swap assumptions; preserve reproducible independent evidence; and perform public-fixture recovery drills on the intended platform and an independent wallet implementation.

**I would not yet use this exact experimental revision as the sole security boundary for large bitcoin holdings.** The central construction is coherent and the targeted evidence is encouraging, but the cleanup defect and deployment/review dependencies remain. Twenty-four words do not cure compromised hardware/software or memory disclosure; optional sources share the same trusted signer. This is a bounded review at one revision, not certification, certainty, or proof of bug freedom.

## Coordinator archive note — 2026-10-14

The planned evidence paths above are now included in [the evidence directory](evidence/seed-6b22f6d/README.md).
The reviewed implementation remains unchanged. See the separate
[boundary review](2026-10-14-opencode-seed-boundaries-6b22f6d.md) and
[finding tracker](FINDINGS.md) for the combined disposition.
