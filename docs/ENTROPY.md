# Recovery-word generation

Experimental work on `feature/seed-generation`, based on
`a008c3bc2a66827ce3bf23ce3f3611229f74a4f6` (preview 7).
The implementation follows the supplied entropy-generation specification v0.1.
It requires independent review and physical-camera testing before merging.

## Sources and backup

Before a key session is loaded, choose an action such as **Share a public key**,
then **Generate new recovery words** when asked for recovery words. Choose the
length and optional sources. To use an existing wallet, choose **Use existing
recovery words** instead. End the current session before generating another wallet.

Generate 12 or 24 English BIP39 words. Import still accepts 12, 15, 18, 21 or 24.
Bitcoin Core 31.1 supplies 32 fresh bytes through `GetStrongRandBytes`; an OS RNG
failure terminates the process. There is no fallback or production test RNG.
Camera samples and physical coin tosses or six-sided die rolls are optional.
All camera/assessment workers finish before manual input or final randomness.
Re-enter the generated words to verify the backup before a key session is created.
Words and contributions are not exported, logged or written to persistent storage.

These sources share the computer and software; independence is not established.
The camera receives no guaranteed entropy credit. Statistical estimates describe
the tested block, not secrecy from an attacker or NIST certification. A compromised
signer, OS or firmware can defeat generation. Repeating physical rolls does not
recover a wallet: retain the generated words and any passphrase.

## Manual collection

- Coins: `T` = heads, `Y` = tails; store outcomes as bytes 0/1. Collect 150/300
  tosses. The model assumes each outcome has conditional probability at most
  0.54, giving approximately 133.35/266.69 bits under that assumption.
- Coin pairs (von Neumann): discard 00/11, map 01 to 0 and 10 to 1. Collect
  128/256 accepted bits, stored as bytes 0/1. Independence and constant bias are
  assumptions, not conclusions of a short test. Fair coins average 512/1024 tosses.
- Die: keys 1–6 become bytes 0–5. Collect 64/128 rolls. These targets were approved
  by the maintainer on 2026-10-13. The model assumes each face's conditional
  probability is at most 1/4; this is not measured for the user's die.

Use real physical outcomes. Backspace corrects the last outcome and recalculates
coin pairs. Repeated outcomes are valid and never filtered. A terminal cannot
distinguish held-key autorepeat from repeated presses: press once per outcome,
check the count, and correct extra entries. Enter explicitly finishes collection;
queued keys cannot approve the next screen. Raw transcripts are bounded at 16,384
outcomes. Their secure buffers are discarded after contribution hashing.

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

Word count and manual type are single bytes. Types are 0 none, 1 coins, 2 coin
pairs, 3 die. An omitted source record is one zero byte; an included record is
its nonzero type (1 for camera), followed by its 32-byte contribution digest.
Take the first 16 bytes of F for 12 words, or all 32 for 24 words, then add the
BIP39 checksum and use the existing pinned English wordlist. Hash contexts and
managed secret buffers are erased; complete physical-memory erasure is not claimed.
The later 64-byte PBKDF2 seed does not increase the mnemonic's 128/256-bit entropy.

A fixed SHA-256 is not a universal statistical extractor. Unpredictability relies
on its cryptographic properties and a secret, unpredictable contribution. Extra
bytes or independent-looking sources do not justify adding security-bit counts.
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
