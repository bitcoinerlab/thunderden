# Audit reports

This directory records reviews of identified Thunder Den revisions. Reports
preserve the reviewer's assessment at the time; [FINDINGS.md](FINDINGS.md) records
the project's current disposition and follow-up evidence. AI reviews are evidence
to evaluate, not security certifications.

## Reports

| Date | Reviewer | Reviewed revision | Report |
| --- | --- | --- | --- |
| 2026-10-14 | OpenCode fresh-context general-agent reviewer | `6b22f6dfb27371f0fb160e4ca8f068f72cfa335e` | [Seed-generation cryptographic review](2026-10-14-opencode-seed-crypto-6b22f6d.md) |
| 2026-10-14 | OpenCode fresh-context explore-agent reviewer; coordinator validation | `6b22f6dfb27371f0fb160e4ca8f068f72cfa335e` | [Seed-generation process and camera boundary review](2026-10-14-opencode-seed-boundaries-6b22f6d.md) |
| 2026-10-12 | OpenCode / OpenAI GPT-6 Astra | [`a5c2f5c`](https://github.com/bitcoinerlab/thunderden/tree/a5c2f5c7f0b39bdba485d9bfd2550d44c2d804e7) | [Independent security review](2026-10-12-gpt-6-astra-a5c2f5c.md) |

The 2026-10-12 report reviews the non-OCR `master` baseline. The 2026-10-14
reports review the experimental seed-generation branch before any audit fixes.
Source references and line numbers refer to the recorded revision. Archiving a
report does not constitute certification or acceptance of all proposed changes;
the tracker records current project dispositions.

## Adding a report

- Name it `YYYY-MM-DD-reviewer-shortcommit.md`; append `-2`, `-3`, etc. if needed
  to distinguish reviews with the same date, reviewer and revision.
- Record the full reviewed commit and any uncommitted changes within scope, the
  review date, reviewer/model identity, scope, methods, checks actually performed
  and limitations. Distinguish source review from validation of a built image;
  identify an image by its hash when one was assessed.
- Preserve the original findings and conclusion after archiving. Mark corrections
  explicitly as dated addenda; record project responses in the findings tracker.
- Add a row to the index and link actionable findings to tracker entries. Give
  new findings stable `TD-001`-style IDs. For duplicates, add the report reference
  to the existing entry while retaining the report's own finding ID.
- Keep evidence shareable: public test data, source references and reproducible
  commands. Promote useful reproductions into regression tests under `tests/`.

## Addressing a finding

1. Validate the claim against the reviewed revision and current code; reproduce
   the behavior where practical.
2. Record a project decision. Distinguish implementation defects, missing
   capabilities, intentional tradeoffs and validation gaps. Consider the proposed
   remedy's complexity as well as its benefit.
3. Make a focused change with a regression check where applicable.
4. Update the tracker with the decision, fix commit, relevant test or verification
   command, and result. Preserve the original report as the historical record.
