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

| ID | Finding | Report priority | Source | Status | Decision / evidence |
| --- | --- | --- | --- | --- | --- |
| TD-001 | Compact multi-input SegWit signing can conceal a higher real fee | High | [2026-10-12 / F-01](2026-10-12-gpt-6-astra-a5c2f5c.md#f-01--compact-multi-input-segwit-fee-attack-remains-possible) | Unreviewed | Report cites an existing executable attack regression and an intentional compatibility tradeoff. Assess the signing-policy recommendation. |
| TD-002 | Direct wallet workflows lack general trusted receive-address verification | High | [2026-10-12 / F-02](2026-10-12-gpt-6-astra-a5c2f5c.md#f-02--receiving-is-insufficiently-protected-in-the-direct-workflows) | Open | Address-QR verification implemented and tested in the working tree. [Decision and verification](#td-002--direct-address-verification); fix commit and physical trial pending. |
| TD-003 | High-index change lacks mandatory recovery-risk disclosure | Medium | [2026-10-12 / F-03](2026-10-12-gpt-6-astra-a5c2f5c.md#f-03--high-index-change-can-make-ordinary-recovery-miss-funds) | Resolved | Fixed in [`f93b234`](https://github.com/bitcoinerlab/thunderden/commit/f93b234d3aa966b485c20523d34b605144569fd6). [Decision and verification](#td-003--change-index-disclosure). |
| TD-004 | Documented Liana/bridge installation follows moving versions | Medium | [2026-10-12 / F-04](2026-10-12-gpt-6-astra-a5c2f5c.md#f-04--lianabridge-setup-is-not-pinned-by-the-readme-workflow) | Resolved | Fixed in [`b1bd501`](https://github.com/bitcoinerlab/thunderden/commit/b1bd50175fa92e3a98c9a6f1b0b078a0b5b7f22e). [Decision and verification](#td-004--pinned-online-installation). |
| TD-005 | Unsigned boot and laptop/firmware trust can defeat signer protections | High impact, conditional | [2026-10-12 / L-01](2026-10-12-gpt-6-astra-a5c2f5c.md#l-01--unsigned-boot-and-general-purpose-laptop-trust-remain-critical) | Unreviewed | Report describes an architectural limitation; no physical exploit was attempted. Assess the supported trust boundary and proposed improvements. |

For each disposition, record the rationale and supporting evidence here. A
resolution should link the fix commit and regression test or verification result.
The original report retains the full threat model, prerequisites and limitations.

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
[`src/review.cpp`](../src/review.cpp) and [`src/application.cpp`](../src/application.cpp).

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
**Fix commit:** [`f93b234`](https://github.com/bitcoinerlab/thunderden/commit/f93b234d3aa966b485c20523d34b605144569fd6)
(`fix: disclose high-index change recovery risk`). TD-003 is resolved at this
source revision; this is not a claim that the fix is in a published USB image.
The original audit report remains unchanged.

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

**Fix commit:** [`b1bd501`](https://github.com/bitcoinerlab/thunderden/commit/b1bd50175fa92e3a98c9a6f1b0b078a0b5b7f22e)
(`docs: pin Liana integration installation versions`). TD-004 is resolved at this
documentation revision. The original audit is unchanged.

## TD-002 — Direct address verification

**Decision (2026-10-12):** accept plain address QRs and the address portion of
`bitcoin:` payment URIs through the existing scanner. Check one explicitly
selected standard account or the compatible multisig wallet already approved in
RAM. Search both receiving and change branches in 1,000-index windows, with a
stable progress display, stop/resume, account selection and exact-index lookup.
The [protocol guide](../docs/PROTOCOL.md#plain-address-checks) describes supported contexts and limits.

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

Implementation and test evidence are recorded; the fix commit and physical-camera
trial are pending. Keep this entry Open until the fix commit is recorded. The
original report is unchanged.
