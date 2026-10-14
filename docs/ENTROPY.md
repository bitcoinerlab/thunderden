# Recovery-word generation

Experimental work on `feature/seed-generation`, based on
`a008c3bc2a66827ce3bf23ce3f3611229f74a4f6` (preview 7).
The implementation follows the supplied entropy-generation specification v0.1,
with manual input simplified to ordinary coin tosses or dice rolls.
It requires independent review and physical-camera testing before merging.

## Sources and backup

Before a key session is loaded, choose an action such as **Share a public key**,
then **Generate new recovery words** when asked for recovery words. Choose the
length and optional sources. To use an existing wallet, choose **Use existing
recovery words** instead. End the current session before generating another wallet.

Generate 12 or 24 English BIP39 words. Import still accepts 12, 15, 18, 21 or 24.

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
Words and contributions are not exported, logged or written to persistent storage.

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
and press `1` to `6`. Record every physical result, including repeats. Backspace
corrects the last entry. Press once per result: a held key can record extra entries.
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
BIP39 checksum and use the existing pinned English wordlist. Hash contexts and
managed secret buffers are erased; complete physical-memory erasure is not claimed.
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
The QR scanner retains its existing conversion contract.

Within each batch: position, then component, then time. Each immutable batch is
constructed once; its byte stream is hashed incrementally and the predetermined
prefix is copied unchanged to a sealed RAM-only memfd for assessment. This is
batch-local temporal grouping, not a whole-capture transpose.
At T=64 the assessed prefix contains the first 16,384 component histories; it is
not assumed to represent all positions or the rest of the capture.

Starting parameters are provisional: 8 warm-up frames, T=64, 1,048,576 assessed
samples, up to 64 MiB hashed, a 20-second capture limit and 120-second total
camera/assessment limit. Never select a profile or retry a block to maximize the
score. Whole batches are serialized; the byte cap may stop at a complete temporal
history. No differences, filters, LSB extraction or bias correction are applied.
Device exposure/gain remain at driver defaults; no characterized control profile
is claimed. Failed capture or assessment is omitted unless a fresh attempt succeeds.
Eight repeated-frame comparisons or eight frames with at least 99% dark/bright
samples trigger whole-contribution rejection. These are fault heuristics, not
calibrated NIST RCT/APT health tests. Even data with a high estimate can be entirely
predictable. The camera never replaces mandatory system randomness.

## Assessment boundary

Use only the official NIST `ea_non_iid` with `-i -a` and eight bits per symbol.
It runs as a fresh executable, separate from the signer and camera, using sealed
sample input and a bounded anonymous JSON output. No sample hash or full JSON is
displayed or logged. The signer uses the existing bounded JSON parser to read the
global `Overall.hAssessed` JSON result; it does not link NIST or jsoncpp.
One OpenMP thread is allowed in production; process limits forbid additional
threads/processes. The assessment has a separate memory/CPU/output budget; scanner
confinement is not weakened. Cancellation kills and waits for every worker.

Pins, runtime dependencies, measured timings and physical-test limitations are
recorded in [dependencies](DEPENDENCIES.md) and [benchmarks](ENTROPY_BENCHMARKS.md). Estimates from synthetic
public fixtures are not evidence of camera unpredictability.
