# Camera-assisted recovery-word entry

Camera entry is experimental on `feature/seed-ocr` and is not included in
`v0.0.1-preview.6`. It proposes an **English BIP39** phrase from paper for local
review and correction. Keyboard entry is available throughout the workflow.

## Capture and review

1. Choose **Read with camera (OCR)** when asked for recovery words.
2. Try to fill the camera view with the words. Keep the writing upright and sharp.
   **Enter** freezes a frame; **Esc** cancels.
3. **Select only the recovery words** in the frozen image. The initial rectangle
   covers detected text with padding, or the full frame if no text is detected.
   It is a starting point, not an automatic decision about which text is a seed.
   The **top-left corner** starts active, marked by a bright handle. **← ↑ → ↓**
   adjust that corner while the opposite corner stays fixed; **Tab** switches
   between top-left and bottom-right. The outside is dimmed. **Enter** reads that
   exact selection; **Esc** retakes the image. Every capture initializes a fresh
   rectangle from the new image, since the paper or camera may have moved.
4. **↑ ↓** selects a word. The list scrolls to keep the selection visible.
   The highest-ranked suggestion is prefilled; `?` marks uncertainty and `*` marks
   a changed/corrected word. A slot without a candidate shows **[Enter word]**.
5. **Enter** focuses the editable word, with an orange highlight and a visible
   cursor. Typing replaces the selected text; normal character editing is also
   available. **Down** enters the attached suggestion list; **Up** from its first
   item returns to the field. Browsing preserves the typed draft. Enter saves the
   field or uses the selected suggestion; Esc cancels the edit. The cursor is
   hidden while a suggestion has focus. Key labels and the current editing
   instruction are orange; action descriptions use the normal text color.
6. Use the controls below to correct extra or missing text. Counts and checksum
   status update after changes. A matching checksum displays **Press C to
   continue** in orange. On short screens C first shows the remaining words;
   after every slot has been displayed, it proceeds to passphrase entry.

| Key | Action outside editing |
| --- | --- |
| ↑ ↓ | Select word |
| Enter | Edit word |
| D or forward Delete/Supr | Request deletion; Enter removes, Esc keeps |
| A / B | Insert above / below; Esc cancels the insertion |
| R | Capture again |
| T | Type words manually |
| C | Continue when the phrase is valid |
| Esc | Go back |

Letter shortcuts enter text while editing. Backspace edits characters; it does
not delete whole words. Cancelling a recapture or manual-entry attempt retains
the current draft. A failed recapture also retains it.

Only **12, 15, 18, 21 or 24 words** with a valid BIP39 checksum can continue.
The editable draft allows **30 slots**, so extra headings or detections can be
deleted. Larger results are rejected rather than silently truncated.
A checksum match does not prove the transcription: a 12-word checksum has only
four bits, and a wrong phrase can still pass it.

## Writing formats

Numbers are optional. Supported numeric prefixes include `1`, `01`, `1.`, `1:`,
`1;`, `1)`, `1-`, `1/`, `(1)` and `[1]`. Numbers can be attached to their words or
detected separately. Ambiguous strings such as `1ion` are not treated as numbered
entries, since OCR can confuse letters and digits.

- Reading order is chosen **once per capture**, after label handling. At least
  four recognized labels can select row-first or column-first traversal when
  only one has an increasing label sequence. Missing labels are ignored for
  that comparison; conflicting labels do not select an order. Without decisive
  labels, one/two text rows read left-to-right, then down. Longer layouts read
  down each column, then left-to-right when persistent horizontal gaps separate
  columns; otherwise they read as paragraph lines. Two and three columns use the
  same rule. Indentation alone does not create a column. Editing and checksum
  suggestions never change the selected order. Repeated words are preserved.
- A standalone number without a readable word retains an empty slot. Numbering
  consistent with the spatial order can expose gaps up to the last observed label.
  Completely missed words on unnumbered sheets may still need manual insertion.
- At least three consistent labels can establish a narrow number area within a
  column. Separate labels and character-aligned bounds of correctly recognized
  attached prefixes both supply evidence. Between those rows, misread or split
  label pieces entirely inside that area are excluded when a separate word is
  aligned beside them. The rule does not guess the damaged label's numeric value.
  Weak numbering and ambiguous geometry do not enable this filtering. An attached
  prefix without character bounds cannot establish a label area; fused, misread
  prefixes may still need manual correction.
- Standalone bullets and writing-rule punctuation do not become seed words.
  Printed underlines and word boxes remain in the image; the pipeline does not
  erase strokes that might also belong to handwriting.
- Character-cell cards can cause OCR to split one word into short fragments.
  Within a single detected region, bounded fragment joining and CTC scoring can
  recover these words. Two already-valid dictionary words are never merged.
- The confirmed rectangle defines what to read. There is no automatic filtering
  of headings, side notes or competing lists after capture. Keep that material
  outside the selection; remaining extra detections can be deleted in the editor.

The supported input is full English BIP39 words. SLIP39 shares, numeric word
indices, punched-dot matrices and QR-encoded backups are different formats.
For example, Trezor's 20-word backup cards are not BIP39 phrases and cannot be
imported through this entry mode.

## Recognition and correction

The worker uses a PP-OCRv5 mobile text detector and English recognizer with CPU
OpenCV. The detector finds text regions; the recognizer returns character
probabilities. **Dictionary-constrained CTC decoding scores all 2,048 BIP39
words for every word span**, including raw readings that already look valid.
It uses a uniform word prior, not ordinary-language word frequencies.

Up to five candidates are retained. Confidence marks uncertainty rather than
hiding an available proposal. The current confidence heuristic requires a 10:1
margin over the runner-up and support relative to the greedy character path;
it is not a calibrated accuracy percentage. Case variants and repeated letters
are handled during CTC alignment. Spaces in identified letter-spaced regions
act as blank alignment steps.

Normalization and bounded edit-distance suggestions remain a fallback for
tokens without probability-based candidates. An initial checksum search explores
at most **64 retained word-candidate combinations** in the established order.
It does not brute-force missing words, fill empty slots or authorize a phrase.
Editing never silently reorders the list.

## Runtime boundaries

`thunderden-ocr` is a separate, short-lived process. It loads pinned models before
camera capture and applies scanner confinement before reading frames. The signer
validates the returned token/candidate schema and performs the local review.
The worker retains the frozen frame while the rectangle is selected, then reads
only the confirmed area. It exits and is reaped before word editing begins.

| Resource | Limit |
| --- | --- |
| Worker virtual address space | 256 MiB |
| Startup, frame preparation and post-selection deadlines | 60 seconds each |
| Negotiated frame size | At most 1920×1080; requests 1280×960 |
| Detector input | Maximum side 768 |
| Contours / retained text regions | 1,024 / 64 |
| Returned OCR tokens | 96 |
| Raw token / candidate text | 32 printable ASCII bytes / one BIP39 word |
| Candidate shortlist | Five distinct words per token |
| Result packet / editable draft | 8 KiB / 30 slots |
| Frozen frame / area-control message | At most 1920×1080 grayscale bytes plus 16 bytes / 16 bytes |

Preview traffic cannot extend frame preparation or recognition deadlines. Local
area selection has no countdown and remains cancellable. Photos and recognized
words are not written to disk or exported over QR. Managed word buffers use the
same secure allocator as keyboard entry; this is not a guarantee of overwriting
every temporary copy inside image-processing libraries. See [isolation](ISOLATION.md).

Models, their derived fixed-shape recognizer, the character dictionary and source
dependencies are pinned in `.env`. Python and ONNX Runtime are build tools only;
neither is installed on the device. OCR libraries are absent from the signer and
QR scanner dependency closures. See [dependencies](DEPENDENCIES.md).

## Tests and layout references

The reproducible fixture corpus is documented in
[`tests/fixtures/ocr/README.md`](../tests/fixtures/ocr/README.md). Generated images
use public test words and generic layouts rather than personal backup photos or
copies of vendor artwork.

```sh
docker compose --progress quiet run --build --rm test
docker compose run --rm test ctest --test-dir /build -R '^(seed-ocr|ocr-engine|ocr-templates|terminal)$' --output-on-failure
```

Coverage includes numbering syntax, ruled and boxed fields, character grids,
two- and four-column cards, headings, missing slots, editing, packet validation
and confined native inference. These structural fixtures are not a handwriting
accuracy benchmark. Real writing styles, camera focus, motion, glare, paper
folds and low resolution can still cause errors.

Public layout references:

- [Trezor printable backup cards](https://trezor.io/learn/security-privacy/personal-security-standards/wallet-backup-cards-download-and-print-for-your-trezor): numbered word boxes and different backup lengths.
- [Coldcard recovery cards](https://coldcard.com/docs/recovery-cards/): individual character cells and nearby PIN/code-word fields.
- [BitBox printable backup card](https://bitbox.swiss/backupcard/backupcard_print.pdf): zero-padded numbering, ruled fields and separate wallet/passphrase information.
- [Ledger recovery-sheet overview](https://www.ledger.com/academy/glossary/recovery-sheet): numbered paper backup fields.

For broader evaluation, [IAM](https://fki.tic.heia-fr.ch/databases/iam-handwriting-database)
and [CVL](https://cvl.tuwien.ac.at/research/cvl-databases/an-off-line-database-for-writer-retrieval-writer-identification-and-word-spotting/)
provide labelled English handwriting; consult their upstream access and licensing
terms. CVL is licensed for non-commercial research. [EMNIST](https://www.nist.gov/itl/products-and-services/emnist-dataset)
provides isolated handwritten letters/digits, useful for character-confusion
tests but not full-card layouts. These datasets do not establish seed-card
performance. A representative benchmark needs held-out writers and layouts,
full wordlist coverage, actual camera captures, and separate measurements of
word accuracy, order, missing/extra slots and shortlist coverage.
