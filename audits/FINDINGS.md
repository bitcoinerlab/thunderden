# Audit findings

This is the current follow-up record for the [audit reports](README.md).
IDs are stable across reports; repeated findings gain source links rather than
new IDs. Report priorities and evidence summaries below are attributed to the
reviewer. **Unreviewed** means a project disposition has not yet been recorded.

## Statuses

- **Unreviewed:** awaiting project assessment.
- **Open:** confirmed and awaiting action. Deferred work stays open with a reason.
- **Resolved:** addressed, with a fix commit and recorded verification.
- **Closed:** no change planned, with an explicit rationale, such as an accepted
  tradeoff or an unsupported claim. This does not mean the reported behavior was
  fixed.

## Findings

### TD-001

Compact multi-input SegWit signing can conceal a higher real fee.

- **Status:** Unreviewed
- **Report priority:** High
- **Source:** [2026-10-12 / F-01][report-f01]

Report cites an existing executable attack regression and an intentional
compatibility tradeoff. Assess the signing-policy recommendation.

### TD-002

Direct wallet workflows lack general trusted receive-address verification.

- **Status:** Open
- **Report priority:** High
- **Source:** [2026-10-12 / F-02][report-f02]

Address-QR verification implemented in [`801b8c7`][commit-801b8c7].
[Decision and verification](#td-002--direct-address-verification);
physical-camera verification pending.

### TD-003

High-index change lacks mandatory recovery-risk disclosure.

- **Status:** Resolved
- **Report priority:** Medium
- **Source:** [2026-10-12 / F-03][report-f03]

Fixed in [`f93b234`][commit-f93b234].
[Decision and verification](#td-003--change-index-disclosure).

### TD-004

Documented Liana/bridge installation follows moving versions.

- **Status:** Resolved
- **Report priority:** Medium
- **Source:** [2026-10-12 / F-04][report-f04]

Fixed in [`b1bd501`][commit-b1bd501].
[Decision and verification](#td-004--pinned-online-installation).

### TD-005

Unsigned boot and laptop/firmware trust can defeat signer protections.

- **Status:** Unreviewed
- **Report priority:** High impact, conditional
- **Source:** [2026-10-12 / L-01][report-l01]

Report describes an architectural limitation; no physical exploit was attempted.
Assess the supported trust boundary and proposed improvements.

### TD-006

Mnemonic validation leaves complete entropy in an uncleansed stack hash context.

- **Status:** Resolved
- **Report priority:** Medium, conditional
- **Source:** [2026-10-14 / CRYPTO-01][report-crypto01]

Fixed by using scope-bound hash cleanup for mnemonic validation and generation.
Optimized-build regressions verify cleanup on success and exceptions.
[Decision and evidence](#td-006--mnemonic-validation-stack-residue).

### TD-007

Native camera capture is incompatible with streaming-only UVC devices.

- **Status:** Open
- **Report priority:** Medium; camera unavailable
- **Source:** [2026-10-14 / BOUNDARY-01][report-boundary01]

Actual linked libv4l2 used raw `read()` and never configured streaming in the
bounded fixture. [Decision and evidence](#td-007--native-camera-streaming).

### TD-008

Exact-sized assessment buffers expose out-of-bounds JSON reads.

- **Status:** Resolved
- **Report priority:** Medium
- **Source:** [2026-10-14 / BOUNDARY-02][report-boundary02]

Fixed with bounded, zero-padded parser storage and early worker-status rejection.
Guard-page regressions verify exact-span input handling.
[Decision and evidence](#td-008--assessment-json-boundary).

### TD-009

Camera address-space limit does not bound anonymous-file memory.

- **Status:** Open
- **Report priority:** Medium, conditional denial of service
- **Source:** [2026-10-14 / BOUNDARY-03][report-boundary03]

Bounded fixture allocated a 40 MiB memfd under a 32 MiB address-space limit.
Requires worker compromise; no decoder exploit was demonstrated.
[Decision and evidence](#td-009--camera-resource-accounting).

### TD-010

Assessment parser does not validate the claimed complete test battery.

- **Status:** Open
- **Report priority:** Low
- **Source:** [2026-10-14 / BOUNDARY-04][report-boundary04]

Existing success fixture accepts ten unidentified method objects plus `Overall`;
diagnostic result only. [Decision and evidence][lower-priority-decisions].

### TD-011

Invalid preferred camera geometry prevents same-device format fallback.

- **Status:** Open
- **Report priority:** Low
- **Source:** [2026-10-14 / BOUNDARY-05][report-boundary05]

Confirmed control-flow gap; physical occurrence remains untested.
[Decision and evidence][lower-priority-decisions].

### TD-012

Twenty-second camera acquisition limit is not a strict cutoff.

- **Status:** Open
- **Report priority:** Low
- **Source:** [2026-10-14 / BOUNDARY-06][report-boundary06]

Blocking capture/send can cross the worker deadline; the parent retains a
separate 30-second watchdog. [Decision and evidence][lower-priority-decisions].

### TD-013

New confinement tests contradict documented unsupported-host skip behavior.

- **Status:** Open
- **Report priority:** Low
- **Source:** [2026-10-14 / BOUNDARY-07][report-boundary07]

Source-level test/documentation mismatch; production remains intentionally
fail-closed. [Decision and evidence][lower-priority-decisions].

## Follow-up evidence

For each disposition, record the rationale and supporting evidence here. A
resolution should link the fix commit and regression test or verification result.
The original report retains the full threat model, prerequisites and limitations.

## TD-006 — Mnemonic validation stack residue

**Decision (2026-10-14):** accept this as a defense-in-depth implementation
defect and keep it open. `ValidateMnemonic` hashes 16 or 32 bytes of reconstructed
entropy in a temporary `CSHA256` object without wiping that object. A public-fixture
probe observed the complete input in the returned stack context and no matching
hash-context cleanse; the encoder's explicit wipe was observed as a positive
control. This defect predates the generation branch, but generation invokes the
validator on newly created recovery words.

The finding does not establish practical extraction. An attacker still needs a
separate memory-disclosure, physical-memory or swap/hibernation path, and none was
demonstrated. That prerequisite does not justify a documentation claim that all
secret hash contexts are erased. Fix the cleanup with scope-bound exception-safe
handling, add an optimized-build regression, and narrow the claim.

**Evidence:** [`residue_probe.cpp`](evidence/seed-6b22f6d/residue_probe.cpp) rebuilt
and reproduced both 16- and 32-byte observations against the reviewed native
image on 2026-10-14. See the [full report](2026-10-14-opencode-seed-crypto-6b22f6d.md)
for independent BIP39, mixing, Core RNG failure and backup/session checks and their
limitations.

**Resolution (2026-10-14):** both mnemonic checksum paths now use the existing
`EntropyHash` cleanup type, shared through `keys.h`. Each temporary hash is erased
at the end of its expression, before checksum comparison or further word
processing, including when hashing throws. The design document now limits its
cleanup statement to the specific contexts and managed buffers covered.

**Regression:** [`tests/mnemonic_cleanup.cpp`](../tests/mnemonic_cleanup.cpp)
links the real optimized application/Core code with test-only observers for hash
finalization and cleansing. It verifies that the complete context is zeroed
while its storage is still alive, without inspecting abandoned stack memory.
Cases cover valid and invalid checksums for 12/15/18/21/24 words, 12-/24-word
generation, and injected exceptions in both checksum paths. The test failed
before the fix with `Mnemonic hash context was not fully cleansed` and passes
afterward. Production symbol checks exclude both test wrappers.

**Verification:** rebuilt the MinSizeRel native targets and ran
`ctest --test-dir /build --output-on-failure`. Sixteen suites passed; `isolation`
correctly rejected the writable build bind mount used by that command. Rerunning
only `isolation` with `/build` read-only and a writable `/build/Testing` tmpfs
passed (11.19 seconds). All 17 suites passed across these runs.
**Fix:** the commit containing this resolution,
`fix: cleanse mnemonic checksum hash contexts`. No new USB image was built.

## TD-007 — Native camera streaming

**Decision (2026-10-14):** accept this as a camera integration defect and keep it
open. Native mode disables libv4l conversion but continues to rely on `v4l2_read()`
to start streaming. The actual linked library instead delegated to raw `read()`
for the synthetic streaming-only UVC interface; no streaming setup was attempted:

```text
registered=1 read_result=-1 errno=22 raw_read_calls=1 streaming_setup_calls=0
```

The fixture establishes the API mismatch but is not a physical-webcam test. The
camera source is optional, so this is not evidence that output becomes predictable
when mandatory Core randomness succeeds. Implement an explicit native streaming
route or another reviewed solution, add a real-library regression and then verify
the exact intended laptop/camera physically.

## TD-008 — Assessment JSON boundary

**Decision (2026-10-14):** accept the trusted-process memory-safety defect and keep
it open. Exact-sized, non-NUL-backed reports ending in truncated number/keyword
tokens caused guard-page SIGSEGVs inside the pinned parser for worker exit codes
0 and 1. Adding a backing NUL outside the same logical span made every control
reject normally. The malformed worker-output prerequisite is meaningful because
the parser is a trust boundary, although no information disclosure or code
execution was demonstrated.

Reject irrelevant failed statuses before parsing and provide parser-safe backing
storage or repair the tokenizer's end checks. Add exact-span malformed-input
regressions. Evidence and all 20 outcomes are in
[`json_boundary.cpp`](evidence/seed-6b22f6d/json_boundary.cpp) and the
[boundary report](2026-10-14-opencode-seed-boundaries-6b22f6d.md).

**Resolution (2026-10-14):** `ParseAssessment` now rejects worker statuses other
than 0 and 255 before parsing. It copies at most 65,536 report bytes into a secure
buffer with five zero padding bytes for the pinned tokenizer's lookahead. The
logical input length remains unchanged, so embedded NULs and trailing data are
not silently discarded. The explicit constant-alphabet exit remains supported.

**Regression:** `AssessmentBoundary` in [`tests/entropy.cpp`](../tests/entropy.cpp)
places exact-sized input against an inaccessible page. It covers truncated
numbers, keywords, escapes, every incomplete prefix of a successful report,
worker statuses 0/1/255/256, embedded NULs, valid constant reports and the maximum
report size. It reproduced SIGSEGV before the fix and passes afterward.

**Verification:** the optimized native build passed `entropy`,
`entropy-assessment` and `terminal` (3/3, 170.78 seconds), using
`ctest --test-dir /build -R '^(entropy|entropy-assessment|terminal)$'
--output-on-failure`. **Fix commit:** [`85520a3`][commit-85520a3]
(`fix: bound assessment JSON parser lookahead`). This is a source fix; the
existing experimental USB image has not been rebuilt.

## TD-009 — Camera resource accounting

**Decision (2026-10-14):** keep this conditional containment gap open. The worker's
`RLIMIT_AS` bounds virtual address space, not memfd backing pages. After applying
the production camera confinement policy, a bounded fixture wrote and released a
40 MiB memfd under a lowered 32 MiB address-space limit in a container capped at
128 MiB with swap disabled:

```text
address_space_limit=33554432 memfd_bytes=41943040 allocated_bytes=41943040
```

This does not demonstrate an OOM or worker compromise. Restrict unnecessary
anonymous-file creation/writing or apply suitable broader accounting, and describe
the current limit specifically as virtual address space. See
[`memfd_budget.cpp`](evidence/seed-6b22f6d/memfd_budget.cpp).

## TD-010 to TD-013 — Lower-priority seed-generation findings

**Decision (2026-10-14):** keep all four open pending focused corrections:

- **TD-010:** validate expected assessment case identities and relevant result
  fields, or narrow the complete-battery claim. This diagnostic never supplies
  guaranteed entropy credit.
- **TD-011:** validate each negotiated format's geometry before ending the format
  preference search. Physical occurrence remains to be established.
- **TD-012:** define and enforce whether 20 seconds is a strict acquisition cutoff
  or a soft worker target distinct from the parent's 30-second watchdog.
- **TD-013:** retain production's fail-closed Landlock requirement while making
  test capability handling or the documented host requirement accurate.

**Combined verification:** the existing native suite passed all 16 suites with no
skips (181.40 seconds). That confirms no ordinary regression in the reviewed build;
it did not expose the findings above. Archived public probes rebuilt cleanly and
reproduced TD-006 through TD-009 on 2026-10-14. The implementation remains at
`6b22f6dfb27371f0fb160e4ca8f068f72cfa335e`; no audit fix is implied by these
records.

## TD-003 — Change-index disclosure

**Decision (2026-10-12):** show every verified change output's amount and index in
mandatory review. For indices strictly above 50,000, also show the full address
or script, a prominent recovery warning and a recovery-specific signing
confirmation. Preserve the existing fee warning when both risks apply and permit
spending already-funded high-index inputs.

The threshold is a disclosure heuristic, not a promise of automatic recovery below
it. The signer has no persistent wallet history or independently known last-used
index. Explicit approval still permits high-index change; this fix addresses the
missing disclosure rather than guaranteeing wallet discovery.

**Reproduction:** the added `ChangeIndices` regression in
[`tests/transactions.cpp`](../tests/transactions.cpp) failed against the prior
implementation with `Mandatory review hid the change index` for verified change
at index 1,000,000,000. The implementation is in
[`src/review.cpp`](../src/review.cpp) and
[`src/application.cpp`](../src/application.cpp).

**Verification:** targeted transaction and terminal tests pass. Coverage includes
BIP84, BIP86 and 2-of-2 P2WSH; indices 6, 50,000, 50,001, 1,000,000,000 and
2,147,483,647; multiple change outputs; no signatures after refusal; and Core
verification of spending high-index receiving and change inputs. The
[`terminal tests`](../tests/terminal.py) exercise mandatory pagination, the full
high-index address, cancellation, typed `SIGN`, completed review and combined
fee/recovery warnings on 40- and 80-column screens.

Full-suite verification on 2026-10-12:

```sh
docker compose --progress quiet run --build --rm test
```

All 11 suites passed, with none skipped (25.02 seconds of test execution). This
checks native behavior, not physical-device recovery or a new release image.
**Fix commit:** [`f93b234`][commit-f93b234]
(`fix: disclose high-index change recovery risk`). TD-003 is resolved at this
source revision; this is not a claim that the fix is in a published USB image.
The original audit report remains unchanged.

**Release availability (2026-10-13):** the change-index disclosure is included in
[preview 7][release-preview7].

## TD-004 — Pinned online installation

**Decision (2026-10-12):** preserve the integration revisions already recorded in
the project status. The [installation guide](../docs/LIANA.md) specifies exact
Liana and async-hwi commits, Rust 1.88.0, checked-in Cargo lockfiles with `--locked`,
and QR bridge `0.1.2`. Both the README and command-line walkthrough now use the
versioned bridge command. The guide records its release-source commit and archive
SHA-256, provides archive verification before execution, and requires a new
verification record when upgrading the pins.

**Verification:** confirmed all three source commits through GitHub. Liana's
manifest and lockfile both select the documented async-hwi commit, its toolchain
file selects Rust 1.88.0, and the named GUI/CLI build targets and CLI lockfile exist.
Downloaded the published bridge using the guide's versioned `npm pack
--ignore-scripts` command: the archive matched the guide's SHA-256
and npm's SHA-512 integrity value; npm's `gitHead` matched the recorded source
commit. The packaged source files matched that commit, the bundled frontend was
present, and the package declared no runtime npm dependencies. A temporary run
with Node.js 26.11.1, `--no-open --port 0`, successfully served the browser page.
Local documentation links, section anchors and shell-snippet syntax also passed
validation.

These are source/pin and published-package checks, not a new Rust build or a
physical wallet/signing validation. The existing integration test scope remains
in [STATUS.md](../docs/STATUS.md#liana-integration).

**Fix commit:** [`b1bd501`][commit-b1bd501]
(`docs: pin Liana integration installation versions`). TD-004 is resolved at this
documentation revision. The original audit is unchanged.

## TD-002 — Direct address verification

**Decision (2026-10-12):** accept plain address QRs and the address portion of
`bitcoin:` payment URIs through the existing scanner. Check one explicitly
selected standard account or the compatible multisig wallet already approved in
RAM. Search both receiving and change branches in 1,000-index windows, with a
stable progress display, stop/resume, account selection and exact-index lookup.
The [protocol guide](../docs/PROTOCOL.md#plain-address-checks) describes supported
contexts and limits.

The signer independently validates the destination and compares public scripts
derived by Bitcoin Core. URI amounts/labels are not payment instructions. A
successful result displays the full address for comparison with the destination
before sending. A range without a match does not imply the address is unrelated
to the wallet. Existing authenticated policy/index requests retain their direct
verification and reply flow; no new policy-loading or signing path is added.

**Verification:** all 12 native suites passed on 2026-10-12 with
`docker compose --progress quiet run --build --rm test` (28.33 seconds; none
skipped). [`tests/address_check.cpp`](../tests/address_check.cpp) covers standard
account types, all selected networks, both branches, wrong accounts/seeds,
Base58/Bech32 case, URI bounds/required parameters, range boundaries and all
supported multisig wrappers. ECDSA/Schnorr signing functions are trapped in that
test executable. [`tests/isolation.cpp`](../tests/isolation.cpp) exercises confined
address-QR decoding and bounded IPC. [`tests/terminal.py`](../tests/terminal.py)
covers full-address display, setup/approval checks, ambiguity, stop/resume,
exact-number entry, resize handling and numeric-only progress updates.

The test image passed installed-content/kernel checks, source-snapshot comparison,
repeat assembly and BIOS/UEFI boot/account-export checks. Its inventory contains
no OCR executable, models or OpenCV. The public fixture producer
`address-check-tests --fixtures` supplies 12-word regtest examples and two multisig
setup QRs for a physical trial; every generated QR matrix is decoded before export.

**Implementation checkpoint (2026-10-13):** [`801b8c7`][commit-801b8c7]
(`feat: verify scanned addresses against wallet accounts`) records the
implementation and automated verification above. Physical-camera verification
remains pending; this entry remains Open. The original report is unchanged.

**Release availability (2026-10-13):** [preview 7][release-preview7]
includes address verification and the review/layout polish from `b6b9c16`.

[report-f01]:
  2026-10-12-gpt-6-astra-a5c2f5c.md#f-01--compact-multi-input-segwit-fee-attack-remains-possible
[report-f02]:
  2026-10-12-gpt-6-astra-a5c2f5c.md#f-02--receiving-is-insufficiently-protected-in-the-direct-workflows
[report-f03]:
  2026-10-12-gpt-6-astra-a5c2f5c.md#f-03--high-index-change-can-make-ordinary-recovery-miss-funds
[report-f04]:
  2026-10-12-gpt-6-astra-a5c2f5c.md#f-04--lianabridge-setup-is-not-pinned-by-the-readme-workflow
[report-l01]:
  2026-10-12-gpt-6-astra-a5c2f5c.md#l-01--unsigned-boot-and-general-purpose-laptop-trust-remain-critical
[report-crypto01]:
  2026-10-14-opencode-seed-crypto-6b22f6d.md#crypto-01-complete-mnemonic-entropy-remains-in-an-uncleansed-stack-hash-context
[report-boundary01]:
  2026-10-14-opencode-seed-boundaries-6b22f6d.md#boundary-01--native-capture-is-incompatible-with-streaming-only-uvc-cameras
[report-boundary02]:
  2026-10-14-opencode-seed-boundaries-6b22f6d.md#boundary-02--exact-sized-assessment-buffers-expose-out-of-bounds-json-reads
[report-boundary03]:
  2026-10-14-opencode-seed-boundaries-6b22f6d.md#boundary-03--address-space-limits-do-not-bound-anonymous-file-memory
[report-boundary04]:
  2026-10-14-opencode-seed-boundaries-6b22f6d.md#boundary-04--report-shape-is-weaker-than-the-complete-battery-claim
[report-boundary05]:
  2026-10-14-opencode-seed-boundaries-6b22f6d.md#boundary-05--invalid-preferred-geometry-prevents-same-device-fallback
[report-boundary06]:
  2026-10-14-opencode-seed-boundaries-6b22f6d.md#boundary-06--twenty-seconds-is-not-a-strict-acquisition-cutoff
[report-boundary07]:
  2026-10-14-opencode-seed-boundaries-6b22f6d.md#boundary-07--unsupported-host-tests-contradict-documented-skip-behavior
[lower-priority-decisions]:
  #td-010-to-td-013--lower-priority-seed-generation-findings
[commit-801b8c7]:
  https://github.com/bitcoinerlab/thunderden/commit/801b8c7bf35b66a5de73e01576a5334c81f1a616
[commit-f93b234]:
  https://github.com/bitcoinerlab/thunderden/commit/f93b234d3aa966b485c20523d34b605144569fd6
[commit-b1bd501]:
  https://github.com/bitcoinerlab/thunderden/commit/b1bd50175fa92e3a98c9a6f1b0b078a0b5b7f22e
[commit-85520a3]:
  https://github.com/bitcoinerlab/thunderden/commit/85520a311e1c1cf182a67a465ea3feb7b99e6909
[release-preview7]:
  https://github.com/bitcoinerlab/thunderden/releases/tag/v0.0.1-preview.7
