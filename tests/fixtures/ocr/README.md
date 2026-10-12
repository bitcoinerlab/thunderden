# OCR layout fixtures

The corpus is generated in memory by [`tests/ocr_templates.cpp`](../../ocr_templates.cpp).
There are no private backup photographs, personal machine paths or downloaded
vendor images in these fixtures. Generation keeps the inputs small, inspectable
and reproducible with the pinned OpenCV build.

## Contents

All filled cards use the published 24-word BIP39 test vector corresponding to
256 bits of `0x7f` entropy. It has nine distinct words. This deliberately narrow
vocabulary exercises **layout and segmentation**, not full-vocabulary handwriting
accuracy.

| Fixture | Property tested |
| --- | --- |
| `colon` | Number followed by `:` |
| `brackets` | `[1]` numbering |
| `parentheses` | `(1)` numbering |
| `zero-padded` | Bare `01`–`24` labels |
| `ruled` | Thin writing lines below words |
| `touching` | Writing lines touching the baseline |
| `boxes` | Boxes around complete words |
| `four-columns` | Four columns of six words |
| `grid` | Lowercase letters in individual character cells |
| `grid-upper` | Uppercase letters in character cells |
| `bullet` | Unnumbered, bullet-prefixed entries |
| `header` | A complete numbered panel with printed instructions above it |

Each filled fixture must produce the exact expected count, ordered words and
checksum under scanner confinement. Additional cases check that a blank card
does not become a usable phrase and that `car pet` remains two words.
Captured images are also tested with BIP39-valued notes and multiple lists; only
the user-selected rectangle is recognized. Parser tests preserve all text within
that selection, incomplete-number columns and regular 13-entry lists. Regression
cases cover indented final rows and assert that checksum assistance never changes
the established order.
Parser regressions also cover misread and split number labels in one/two columns,
genuine short words, word fragments, weak/conflicting numbering and ambiguous
label boundaries. These token-level cases isolate layout handling from inference.
An additional 24-entry fixture misreads the second column's `10-` as a separate
`top`, with standalone, attached and mixed prefixes. It checks exact row-first
order and preservation of genuine `top` words through the CBOR boundary. Ordering
cases cover weak/conflicting clues, two/three/six columns, short lines and paragraphs.

[`tests/ocr_engine.cpp`](../../ocr_engine.cpp) covers one/two/three columns,
numbered/unnumbered 12- and 24-word sheets, mixed attached separators, prefix
geometry, row-first numbering, two-line phrases and CTC
probability edge cases. [`tests/seed_ocr.cpp`](../../seed_ocr.cpp) covers parser
bounds, missing slots and ambiguous numbering. Framebuffer and isolation tests
cover two-corner area selection, Tab focus, fixed opposite corners, dimming and
the frozen-frame/crop handoff between processes.

## Run

From the repository root:

```sh
docker compose --progress quiet run --build --rm test
docker compose run --rm test ctest --test-dir /build -R '^ocr-templates$' --output-on-failure
```

The fixtures model common recovery-card structures described in the
[OCR guide](../../../docs/OCR.md#tests-and-layout-references). They are generic
drawings, not replicas or endorsements of particular vendors' cards.

## Extending coverage

Add a minimal, public input that reproduces the failure and assert the intended
word count/order rather than just vocabulary membership or checksum success.
Keep ordinary adjacent words, incomplete numbering and unnumbered layouts in
negative coverage when changing grouping/filtering rules.

A future handwriting corpus should cover the entire English BIP39 list, diverse
writers, pens, lighting and capture resolutions, with writer- and layout-disjoint
evaluation sets. Report exact words, ordering, missing/extra slots and shortlist
coverage separately. Printed fonts and geometric distortions alone cannot supply
that evidence.
