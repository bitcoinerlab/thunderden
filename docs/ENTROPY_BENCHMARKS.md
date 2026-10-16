# Entropy generation: verification and measurements

Experimental branch, measured 2026-10-13/14. These are software/lab results, not
an entropy-source certification or a performance promise for an older laptop.
Physical camera and reference-laptop measurements are pending maintainer testing.

These historical measurements used `-i -a`. Production now uses the official
`-i -t` mode: all selected original symbols are assessed, but separate bitstring
checks are limited to the first 1,000,000 bits. The tables below are not timings
or estimates for that mode; the synthetic matrix intentionally retains `-a` for
full-bitstring comparisons. The assessment regression compares the isolated
production wrapper with the official `-i -t` entry point on identical public data.

## Environment and method

- Development CPU: AMD Ryzen 7 8745HS, x86-64, 16 logical CPUs; about 58.6 GiB RAM.
- Host kernel: Linux 7.2.5, Landlock ABI 6 or newer available.
- Native compiler: GCC 14.2.0 in the pinned Debian container; jsoncpp 1.9.6.
- Core 31.1; NIST `87c104d0`; divsufsort `5f60d6f0`. Full pins/hashes are in `.env`.
- NIST: upstream `-O2 -ffloat-store`, `-i -a`, 8-bit samples, no `-march=native`.
  Target x86-64 baseline is unchanged. Core/OpenSSL may select supported CPU
  acceleration at runtime. These measurements do not characterize other CPUs.
- Sample streams are public deterministic fixtures. The reference permutation in
  the Python matrix is excluded from NIST time; production C++ costs are below.
- The one-thread matrix ran alongside an image build. The two-thread runs were
  later, so their wall-time difference is not a controlled speedup measurement.
  Thread counts below are requested OpenMP settings; production additionally
  enforces a no-new-thread/process resource limit.
- RSS is the child process's kernel-reported peak, not an estimate from file size.

## Official estimator observations

T=64; samples are bytes. `Timeout` means no complete result was obtained within
120 seconds. It is not a low score or a successful assessment.

| Public fixture / order | Samples | Threads | Seconds | Peak RSS KiB | Overall bits/sample |
| --- | ---: | ---: | ---: | ---: | ---: |
| Constant / either | 1,048,576 | 1 | 0.02 | 18,632–23,124 | Constant-alphabet diagnostic; exit 255 |
| Periodic / frame-major | 1,048,576 | 1 | 7.899 | 247,700 | 0 |
| Periodic / temporal | 1,048,576 | 1 | 120.006 | 245,548 | Timeout |
| Repeated 4096-byte block / frame-major | 1,048,576 | 1 | 55.691 | 247,244 | 0.000001513 |
| Repeated 4096-byte block / temporal | 1,048,576 | 1 | 120.024 | 207,000 | Timeout |
| Fixed texture / frame-major | 1,048,576 | 1 | 120.021 | 245,440 | Timeout |
| Fixed texture / temporal | 1,048,576 | 1 | 5.284 | 240,768 | 0.022165 |
| Markov / frame-major | 1,048,576 | 1 | 5.666 | 250,852 | 0.028368 |
| Markov / temporal | 1,048,576 | 1 | 7.515 | 268,860 | 0.028985 |
| Public PRNG / frame-major | 1,048,576 | 1 | 14.147 | 297,504 | 7.105713 |
| Public PRNG / temporal | 1,048,576 | 1 | 14.689 | 297,492 | 7.351884 |
| Public PRNG / frame-major | 2,097,152 | 1 | 31.994 | 307,756 | 7.310227 |
| Public PRNG / temporal | 2,097,152 | 1 | 32.861 | 307,820 | 6.906122 |
| Public PRNG / temporal, T=32 | 1,048,576 | 2 | 12.463 | 297,900 | 6.655535 |
| Public PRNG / temporal, T=64 | 1,048,576 | 2 | 12.381 | 298,112 | 7.351884 |
| Public PRNG / temporal, T=128 | 1,048,576 | 2 | 13.164 | 297,980 | 7.303856 |

The full one-thread matrix also covered all six patterns at 2,097,152 samples.
Periodic/loop temporal streams and frame-major fixed texture again timed out.
Peak RSS reached 488,128 KiB for the 2 MiB periodic frame-major case. These raw
development runs do not imply that every 2 MiB case fits the production wrapper's
512 MiB **virtual-address-space** limit.

The separate production-wrapper test on a public 1 MiB SHAKE fixture completed in
13.464 seconds (13.354 CPU seconds), with 297,116 KiB RSS and a 7.294433 estimate.
Its JSON input hash matched the sealed sample bytes exactly. **Known public data
can score highly.** Reordering changes estimator behavior without creating secret
information; the profile is not selected to maximize its score.

## Production C++ serialization and hashing

Public 640×480 fixtures; complete batches, 65,536-byte transport chunks. These
measure `TemporalSamples` and incremental Core SHA-256, excluding capture/NIST.

| Components | T | Samples | Serialization seconds | Core SHA seconds |
| --- | ---: | ---: | ---: | ---: |
| Y | 32 | 9,830,400 | 0.029869 | 0.045333 |
| Y | 64 | 19,660,800 | 0.069406 | 0.090158 |
| Y | 128 | 39,321,600 | 0.143357 | 0.178530 |
| RGB | 32 | 29,491,200 | 0.089950 | 0.132161 |
| RGB | 64 | 58,982,400 | 0.205902 | 0.266777 |
| RGB | 128 | 117,964,800 | 0.429637 | 0.521865 |

The resulting first profile keeps 1 MiB assessed, T=64, one assessment thread,
512 MiB virtual-address-space limit and 120-second total deadline. These remain
provisional pending real-camera/older-laptop measurements. The matrix demonstrates
why a universal two-minute completion claim would be incorrect.

## Reproduce the measurements

Build the native test image, then run these development-only commands:

```sh
docker compose run --build --rm test
docker compose run --rm test /build/entropy-benchmark
docker compose run --rm test python3 -B /work/tests/entropy_benchmark.py --frames 64 --threads 1
docker compose run --rm test python3 -B /work/tests/entropy_benchmark.py --samples 1048576 --frames 32 64 128 --threads 2 --patterns public-prng
docker compose run --rm test python3 -B /work/tests/entropy_selftest.py /build/ea_non_iid
```

On a Linux lab machine with Landlock ABI 6, grant the container access to a camera:

```sh
docker compose run --rm --device /dev/video0 --group-add "$(stat -c %g /dev/video0)" test /build/entropy-benchmark --camera
docker compose run --rm --device /dev/video0 --group-add "$(stat -c %g /dev/video0)" test /build/entropy-benchmark --camera --serial
```

This lab command captures in its keyless benchmark process and runs the real
isolated assessment worker. It reports dimensions/fourcc, frames, actual hashed
volume, warm-up, time spent in capture calls, serialization, hashing, assessment,
total time and process RSS. Capture-call time includes warm-up; do not add those
two values. Concurrent assessment completion is observed between batches, so that
timing may include observation delay. It emits no samples or sample hashes.
Record the laptop/CPU, available RAM, camera model and lighting alongside the
output. Compare still scenes, low light, saturation and disconnects. These lab
captures must never be used as wallet-generation material.

## Verification scope

All 16 native suites passed on 2026-10-14 (185.28 seconds; none skipped), including
the real 120-second timeout path. Tests cover independent mixing/BIP39 vectors,
native format negotiation and padding, sealed-file assessment, constant-data
status, complete official results, malformed streams, frozen/stalled capture,
no workers during manual input/final RNG, backup mismatch, cancellation, import
regressions and process/thread/memory limits. Production binaries have no test
RNG hook; image checks exclude all test/benchmark executables.

All 11 upstream self-test datasets passed the original comparator's stricter
1e-10 criterion as well as the README's 1e-6 tolerance. Largest reported delta:
1.00808250635964e-13. This validates numerical compatibility for that corpus,
not the physical entropy of any webcam.

Physical camera throughput, old-laptop timing, lighting behavior and the final
on-device generation/backup workflow remain for the maintainer's trial. Independent
review of the new secret/process boundaries is also pending before merge.

After simplifying manual input, all 16 suites passed again (182.50 seconds).
The follow-up H/T key change passed the entropy and terminal suites (153.54 seconds),
including mixed-case keys, corrections, collection limits and the complete
camera/dice/backup flow. Manual input now holds one target-sized buffer; runtime
source is 4,778 lines, 11 fewer than the initial implementation below.

## Initial test image and added footprint

The initial 64 MiB test image uses application source `77bf073` and has SHA-256:

```text
e40261df09e323ec6ebf8063fdf2c3f60f4113dca1088926ffb95ef6b3c95bea
```

Its toolchain and target were compiled in a fresh output volume with OpenMP
enabled. Installed-file/kernel checks and deterministic repeat assembly passed.
BIOS and UEFI QEMU boot/account-export/session checks passed at 1280x800.
A separate test-only guest overlay completed the isolated official NIST battery
on a public binary fixture and generated valid, fresh 12/24-word phrases through
the real system RNG. It printed no words. This checks the image kernel/runtime
and baseline QEMU CPU, not a physical webcam.

| Installed component | Bytes |
| --- | ---: |
| Signer | 2,681,328 (+45,248 from preview 7) |
| QR scanner | 96,368 |
| Native camera entropy worker | 26,712 |
| Confined NIST worker | 211,848 |
| libdivsufsort / libdivsufsort64 | 42,776 each |
| libgomp | 323,144 |
| jsoncpp 1.9.7 | 272,320 |
| NIST notice | 7,497 |

The installed regular-file total grew by 972,321 bytes; the disk image remains
64 MiB. Project-owned runtime source grew from 4,045 to 4,789 physical lines
(+744, including comments and blanks). The README's broader build/platform scope
is about 5,800 lines. These counts exclude upstream estimator/library sources;
the dependency and isolation cost is real even though the signer does not load them.

Use a fresh output volume when first building this branch: enabling OpenMP changes
the toolchain. Downloads can be reused. For example:

```sh
docker compose run --build --name thunderden-seed-image -v thunderden-seed-out:/cache/out image
```

Use the [build guide's copy/verification steps](BUILD.md#1-build-the-image) with
that container name. The experimental image is for the agreed hardware trial;
it has not been merged into `master` or published as a release.
