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
| TD-003 | High-index change lacks mandatory recovery-risk disclosure | Medium | [2026-10-12 / F-03](2026-10-12-gpt-6-astra-a5c2f5c.md#f-03--high-index-change-can-make-ordinary-recovery-miss-funds) | Unreviewed | Report describes six successful adversarial probes. Reproduce the behavior and assess mandatory change-path disclosure. |
| TD-004 | Documented Liana/bridge installation follows moving versions | Medium | [2026-10-12 / F-04](2026-10-12-gpt-6-astra-a5c2f5c.md#f-04--lianabridge-setup-is-not-pinned-by-the-readme-workflow) | Unreviewed | Report cites moving fork revisions and an unversioned package command. Review installation reproducibility and tested-version references. |
| TD-005 | Unsigned boot and laptop/firmware trust can defeat signer protections | High impact, conditional | [2026-10-12 / L-01](2026-10-12-gpt-6-astra-a5c2f5c.md#l-01--unsigned-boot-and-general-purpose-laptop-trust-remain-critical) | Unreviewed | Report describes an architectural limitation; no physical exploit was attempted. Assess the supported trust boundary and proposed improvements. |

For each disposition, record the rationale and supporting evidence here. A
resolution should link the fix commit and regression test or verification result.
The original report retains the full threat model, prerequisites and limitations.
