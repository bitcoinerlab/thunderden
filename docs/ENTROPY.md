# Recovery-word generation

Included in `v0.0.1-preview.8`, merged into `master` on 2026-10-14.
The implementation follows the supplied entropy-generation specification v0.1,
with manual input simplified to ordinary coin tosses or dice rolls.
Independent AI review reports and fixes are recorded in the
[audit findings](../audits/FINDINGS.md). User-reported physical camera checks are
recorded in [implementation status](STATUS.md#current-release-candidate-2026-10-16).
Broader camera measurements and further independent review remain separate work.

## Sources and backup

From preview 10, choose **Create a new wallet** on the initial **Start a session**
menu, then choose the length and optional sources. To use an existing wallet,
choose **Enter recovery words** or **Scan a seed QR** instead. End the current
session before generating another wallet. Preview 9 still asks for recovery words
when first opening a wallet action, such as **Share a public key**.

Generate 12 or 24 English BIP39 words. The chooser recommends 12 words for strong
protection and an easier backup; 24 words provide a larger recovery-phrase search
space, with more words to write down and check. Details explain the 128/256-bit
lengths. Import still accepts 12, 15, 18, 21 or 24 words.

The flow explains Bitcoin Core's random-number generator once, then introduces
camera readings and coin tosses or dice rolls as optional additions. The camera
chooser defaults to "Use camera", with "Skip camera" second. Manual input still
defaults to "Continue without coins or dice". Required toss/roll counts are shown
before selection. Instructions remain available on compact screens through paging
rather than being silently hidden.

Current source completes the camera preparation, collection, checks and review
before asking about coins or dice. The still-scene hint appears directly above
the Start instruction. These flow changes are included from preview 11.

**System randomness is the main source.** Bitcoin Core's strong random number
generator (RNG) gets fresh randomness from the operating system. It can also use
CPU hardware randomness when available.

Camera samples, coin tosses and dice rolls are **optional extras**. They may help
if a source is weaker than expected. The system RNG is still used every time.
**If it reports an error, generation stops**, even if extra sources were selected.
For example, a failed camera can be left out; it cannot replace a failed system RNG.

Core 31.1 supplies 32 fresh bytes through `GetStrongRandBytes` for each generation.
All camera/assessment workers finish before manual input or final randomness.
Re-enter the generated words to verify the backup before a key session is created.
Before revealing the words, the interface asks the user to keep other people and
cameras from seeing the screen and to write every word down in order.
From preview 10, a private [seed QR backup](SEED_QR.md) is shown beside the words by
default; **F** switches Standard/Compact and **Q** hides/shows it. The QR is the
same recovery secret: only scan it with a trusted offline device. The written-word
check remains required. Contributions are not exported; neither words nor
contributions are logged or written to persistent storage.

All inputs pass through the same computer. More sources do not protect against
compromised signer software, OS or firmware. A camera score is a diagnostic, not
a guarantee of randomness or NIST certification. Repeating the same tosses or
rolls does not recover the wallet: back up the generated words and any passphrase.

## Manual collection

| Method | 12 words | 24 words |
| --- | ---: | ---: |
| Coin tosses | 150 tosses | 300 tosses |
| Dice rolls | 64 rolls | 128 rolls |

For coins, press `H` for heads or `T` for tails. For dice, use one six-sided die
and press `1` to `6`. Enter every physical result, including repeats. Backspace
removes the last entry. Press once for each result.
Enter finishes collection; queued keys cannot approve the next screen. The secure
buffer holds only the required number of results, then is cleared after hashing.

### What the targets assume

- **Coins:** a fair coin is 50/50. Our model allows up to a 54% chance for either
  result. For example, 54% heads and 46% tails fits; 60/40 is outside the assumption.
- **Dice:** a fair die gives each face about 16.7%. Our model allows up to 25% for
  any face. For example, 25% for six and 15% for each other face adds up to 100%.

These are chances for the **next result**, even when earlier results are known.
They are not limits on the counts you record: 18 sixes in 64 rolls can happen by
chance. Nor do balanced totals prove randomness: a repeating heads/tails pattern
is predictable. Thunder Den does not measure these assumptions for your coin or die.

For technical review, the bound is `H_min >= -n * log2(q)`, where `q` limits each
outcome's probability given its history and the attacker's information. The coin
targets give about 133.35/266.69 bits under the 0.54 model; dice give 128/256 under
the 0.25 model. These are model bounds, not measured security guarantees.

## Versioned mixing

SHA-256 is the existing Core implementation. `D(label)` is ASCII plus one NUL.
`FIELD(tag, bytes)` is one tag byte, an unsigned big-endian 64-bit byte length,
then the bytes. Fields appear in the order below. No native C++ struct layout,
timestamps, estimator scores or device names enter this format.

```
C_raw = SHA256(serialized camera samples)
C = SHA256(D("ThunderDen/entropy/camera/v1")
    || FIELD(1, profile) || FIELD(2, BE64(sample_count)) || FIELD(3, C_raw))
M = SHA256(D("ThunderDen/entropy/manual/v1")
    || FIELD(1, manual_type) || FIELD(2, confirmed_manual_samples))
F = SHA256(D("ThunderDen/entropy/final/v1")
    || FIELD(1, word_count) || FIELD(2, camera_record)
    || FIELD(3, manual_record) || FIELD(4, fresh_32_byte_system_randomness))
```

Coins are encoded as bytes (heads = 0, tails = 1); dice faces 1–6 as bytes 0–5.
Word count and manual type are single bytes. Types are 0 none, 1 coins and 3 dice;
the retired type 2 is rejected. The remaining v1 tags and vectors are unchanged.
An omitted source record is one zero byte; an included record is
its nonzero type (1 for camera), followed by its 32-byte contribution digest.
Take the first 16 bytes of F for 12 words, or all 32 for 24 words, then add the
BIP39 checksum and use the existing pinned English wordlist. The SHA-256 contexts
used for entropy mixing and mnemonic checksums are erased on scope exit, including
exceptions. Managed secret buffers are erased on deallocation. This does not
establish erasure of every cryptographic-library temporary or physical-memory copy.
The later 64-byte PBKDF2 seed does not increase the mnemonic's 128/256-bit entropy.

SHA-256 mixes the inputs; it does not create randomness. The design relies on
Core's strong RNG and SHA-256. It does not claim a mathematical entropy guarantee
for every possible input, or add up security bits from the different sources.
Tests include independently calculated specification vectors; these public inputs
are never a source of generation randomness.

## Camera profile v1

The 17-byte profile is: version 1 (one byte), negotiated V4L2 fourcc (BE32),
width (BE32), height (BE32), selection 0 (all positions, row-major), components
(1 = native Y, 3 = R/G/B), frames per batch (BE16). Prefer native GREY, YUYV,
UYVY, then RGB24/BGR24. Disable libv4l format conversion. Y is copied directly;
BGR is reordered to RGB without channel mixing. Padding is excluded. Other
formats, including MJPEG-only cameras, are not supported by this first profile.
Native capture requires V4L2 streaming: it maps and queues driver buffers, then
dequeues and requeues frames on a nonblocking descriptor. Conversion stays off.
The QR scanner retains its existing conversion contract.

Within each batch: position, then component, then time. Each immutable batch is
constructed once; its byte stream is hashed incrementally and the predetermined
prefix is copied unchanged to a sealed RAM-only memfd for assessment. This is
batch-local temporal grouping, not a whole-capture transpose.
At T=64 the assessed prefix contains the first 16,384 component histories; it is
not assumed to represent all positions or the rest of the capture.

Starting parameters are provisional: 8 warm-up frames, T=64, 1,048,576 assessed
samples, up to 64 MiB hashed and a 20-second acquisition window after device setup.
There is no application-imposed wall-clock or CPU deadline for the assessment.
Frames returned at or after the acquisition cutoff are discarded. Already acquired
complete batches may finish
serialization afterward; the parent has a separate 30-second capture watchdog
covering startup, draining and camera-worker exit. No frame-time guarantee applies
to a hung kernel driver. Never select a profile or retry a block to maximize the
score. Whole batches are serialized; the byte cap may stop at a complete temporal
history. No differences, filters, LSB extraction or bias correction are applied.
Device exposure/gain remain at driver defaults; no characterized control profile
is claimed. Failed capture is omitted unless a fresh attempt succeeds. An interrupted
assessment can be included only as an explicitly unchecked extra after complete,
fault-free capture, as described below.
Eight repeated-frame comparisons or eight frames with at least 99% dark/bright
samples trigger whole-contribution rejection. These are fault heuristics, not
calibrated NIST RCT/APT health tests. Even data with a high estimate can be entirely
predictable. The camera never replaces mandatory system randomness.

## Assessment boundary

The interface introduces NIST as the US National Institute of Standards and
Technology and explains that the randomness checks help estimate how unpredictable
the checked data is. Collection and checks can overlap. The live screen shows
the current check, a numbered sequence of nine check groups, current-pass counts
and percentages where available, and elapsed time. Tuple and LRS share one group
because the pinned implementation calculates both together. Bitstring and original
values are separate passes, so their counters can restart within a check. These
are not overall time percentages. Compression and suffix-array work show activity
instead of a fabricated sample percentage.

During capture, **Esc: stop camera** stops and reaps the workers, discards the
entire attempt and offers **Continue without camera data** (the default) or
**Try camera again**, without automatically cancelling wallet creation. Partial
captures cannot be included, even if some statistical checks have finished.
After complete,
fault-free capture and a successful camera-worker exit, **Esc: stop checks** stops
and reaps every worker before offering a choice. Checks continue until completion,
manual stop or a resource/data failure; a two-minute timer does not stop them. The
chooser defaults to **Continue without camera data**. **Use unchecked camera data** is an
explicit opt-in; the final generation confirmation also labels that contribution
unchecked. Fresh Core randomness remains mandatory and is requested only afterward.
The chooser explains that camera data may add useful randomness but never replaces
or weakens Core's protection. This describes the cryptographic mixing design under
the existing Core RNG/SHA-256 assumptions, not a quantified credit for unchecked
data or a guarantee about compromised software or hardware.

The stopped-checks screen states that there is no final randomness estimate.
Details show only finished individual results, with the warning that unfinished
checks could lower the result. They are not an early-acceptance score, an entropy
credit or a replacement for the missing estimates. Low nonzero or zero individual
estimates alone are not an invented rejection threshold. Known constant input,
frozen/dark/bright capture faults, malformed progress and invalid capture streams
cannot be accepted through the unchecked path. Failures offer retry or continuation
without camera data. Successful full results retain their short confirmation;
warnings remain visibly distinguished.

The summary shows estimated randomness in checked data, in bits per camera value,
and the number of camera values included. A visible prompt offers Details with
`D` (or `d`) or Enter to continue and use the camera data. Details state how many
of the collected camera values the randomness checks examined in one sentence.
The estimate describes that tested data; the interface does not extrapolate a
total number of bits added to the wallet.

Use the pinned official NIST `ea_non_iid` estimators with `-i -t` and eight bits
per symbol, with progress-only instrumentation in a build-local source copy.
The original-symbol estimators still examine all 1,048,576 selected component
values. NIST's `-t` option limits the separate bitstring assessment to the first
1,000,000 bits, rather than all 8,388,608 bits of that input. This reduces that
branch's workload and coverage; it is not an equivalent full-bitstring assessment
or a guarantee of completion on slow hardware. The fixed prefix must not be
selected or retried to improve an estimate. Camera acquisition, sample selection,
hashing, memory/output limits and mandatory system randomness are unchanged. Details
show the bitstring limit alongside the original-symbol sample count.
It runs as a fresh executable, separate from the signer and camera, using sealed
sample input and a bounded anonymous JSON output. No sample hash or full JSON is
displayed or logged. The signer uses the existing bounded JSON parser to read the
global `Overall.hAssessed` JSON result; it does not link NIST or jsoncpp.
Descriptor 5 carries fixed 20-byte progress records on a nonblocking pipe, separate
from suppressed estimator stdout/stderr and the final report. The signer bounds
record counts, validates identities, ranges and finite estimates, and buffers
fragmented records. Sequential-loop updates are rate-limited to one per second;
full pipes omit updates rather than block computation. Stage starts and individual
results are also reported. No raw readings, hashes, filenames or estimator text
cross this progress channel. Missing updates can undercount finished work; only
the validated full report can supply a final estimate.
The parser requires the pinned eleven-case sequence and validates the applicable
literal/bitstring result fields and numeric ranges. Upstream can omit an
unavailable compression, tuple or predictor estimate; that produces a warning,
not an unqualified complete result. Missing mandatory results are rejected.
Schema checks do not prove that a compromised worker ran the estimators.
One OpenMP thread is allowed in production; process limits forbid additional
threads/processes. The assessment has a separate memory/output budget; scanner
confinement is not weakened. It does not install a CPU limit or override inherited
OS CPU limits. Capture and stall watchdogs remain enforced. Cancellation kills and
waits for every worker.

Pins, runtime dependencies, measured timings and physical-test limitations are
recorded in [dependencies](DEPENDENCIES.md) and [benchmarks](ENTROPY_BENCHMARKS.md). Estimates from synthetic
public fixtures are not evidence of camera unpredictability.
