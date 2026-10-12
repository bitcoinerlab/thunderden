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
| TD-002 | Direct wallet workflows lack general trusted receive-address verification | High | [2026-10-12 / F-02](2026-10-12-gpt-6-astra-a5c2f5c.md#f-02--receiving-is-insufficiently-protected-in-the-direct-workflows) | Unreviewed | Report identifies a gap in direct Sparrow/Keeper workflows and distinguishes it from address display through the custom command protocol. |
| TD-003 | High-index change lacks mandatory recovery-risk disclosure | Medium | [2026-10-12 / F-03](2026-10-12-gpt-6-astra-a5c2f5c.md#f-03--high-index-change-can-make-ordinary-recovery-miss-funds) | Resolved | Fixed in [`f93b234`](https://github.com/bitcoinerlab/thunderden/commit/f93b234d3aa966b485c20523d34b605144569fd6). [Decision and verification](#td-003--change-index-disclosure). |
| TD-004 | Documented Liana/bridge installation follows moving versions | Medium | [2026-10-12 / F-04](2026-10-12-gpt-6-astra-a5c2f5c.md#f-04--lianabridge-setup-is-not-pinned-by-the-readme-workflow) | Unreviewed | Report cites moving fork revisions and an unversioned package command. Review installation reproducibility and tested-version references. |
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
