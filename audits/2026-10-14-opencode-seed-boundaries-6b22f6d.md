# Independent review: seed-generation process and camera boundaries

**Reviewed revision:** `6b22f6dfb27371f0fb160e4ca8f068f72cfa335e`,
`feature/seed-generation`, compared with `a008c3bc2a66827ce3bf23ce3f3611229f74a4f6`.
**Date:** 2026-10-14.

**Review staffing:** a fresh-context OpenCode explore-agent reviewer performed
read-only source analysis independently of the implementing conversation. The
coordinator, OpenCode / OpenAI GPT-6 Astra, then validated the principal claims
with bounded public-fixture probes and consolidated this report. The reviewer
backend model/effort was not separately recorded. An earlier general-agent
boundary-review attempt did not complete; its artifacts are not relied upon here.
This is AI-assisted review, not an independent professional certification.

The separate [cryptographic review](2026-10-14-opencode-seed-crypto-6b22f6d.md)
covers mixing, BIP39, actual Core RNG failures, manual input and backup/session
behavior. Its confirmed stack-cleanup finding is part of the combined conclusion.

## Conclusion

**This revision is not ready to be relied upon as the sole security boundary for
large Bitcoin holdings.** The generation construction has encouraging independent
functional evidence, but the camera integration is defective and malformed
assessment reports expose an unsafe parser boundary. There is also a conditional
resource-containment gap and several smaller correctness/verification issues.

These findings do not demonstrate predictable output from a healthy Core RNG,
extraction of an actual wallet, or remote code execution. Correct BIP39 vectors
and a passing test suite do not establish the absence of these other defects.

## Scope and baseline evidence

The source review covered `src/camera.*`, `camera_entropy.*`,
`camera_entropy_main.cpp`, `entropy_capture.cpp`, `entropy_process.*`,
`assessment_main.cpp`, `isolation.*`, their tests, CMake/build/platform integration,
and the corresponding documentation. Relevant pinned upstream libv4l, Core
UniValue, Linux UVC/Landlock, and NIST source was examined.

The coordinator reran the existing native suite: **16/16 passed, no skips,
181.40 seconds**, using the previously built `thunderden-v2-test` image. The
cryptographic reviewer independently matched its source/test files to the reviewed
revision. Its image ID is recorded in that report. Production source remained
unchanged during this audit; the only repository additions are audit records and
public evidence.

The local USB image and its archived copy both have SHA-256:

```text
51608eff0713ef159817f8084d69312944871146da6c2815cb33883395d95b5c
```

Its saved compiled `src/` snapshot matches the reviewed source. This audit checked
that correspondence and hash; it did not independently rebuild the whole image,
repeat its historical QEMU checks, or perform physical-camera testing. The archive
already records BIOS/UEFI verification for that artifact. Neither source-snapshot
agreement nor an adjacent checksum authenticates firmware or every later boot.

The [public evidence directory](evidence/seed-6b22f6d/README.md) contains the
coordinator's minimal parser, camera-API and memory-budget reproductions, plus the
cryptographic reviewer's residue probe. Probes run in disposable, network-disabled,
capability-dropped containers, with no wallet secrets or physical device access.

## BOUNDARY-01 — Native capture is incompatible with streaming-only UVC cameras

**Priority:** Medium, functional availability; high impact within the camera feature.
**Tracker:** TD-007. **Confidence:** High, source plus real-library reproduction.

At `src/camera.cpp:42–44`, native mode calls
`v4l2_fd_open(fd, V4L2_DISABLE_CONVERSION)`. At lines 111–117 it still relies on
`v4l2_read()` to start capture. In the pinned libv4l 1.28.1 implementation,
disabling conversion leaves the conversion pointer null. `v4l2_read()` then
calls the device's raw read operation, bypassing streaming-buffer setup and
`STREAMON` emulation. The pinned Linux UVC driver has streaming operations but
no read operation; the V4L2 core returns `EINVAL` on that path.

The coordinator exercised the **actual linked libv4l2 API**, replacing only the
device syscalls with a streaming-only interface that advertises native YUYV:

```text
registered=1 read_result=-1 errno=22 raw_read_calls=1 streaming_setup_calls=0
```

This is not a physical-webcam test. It confirms the API mismatch independently of
the unit tests, which replace `v4l2_fd_open` and `v4l2_read` themselves. Synthetic
integration camera workers also cannot reveal this mismatch.

**Effect:** A normal UVC camera can negotiate a supported native format and still
fail its first capture. The optional contribution is omitted through the failure
flow. This is separate from the intentional exclusion of MJPEG-only cameras and
does not prove weak seeds when mandatory system randomness succeeds. The existing
conversion-enabled QR scanner does not take the same failing branch.

**Recommended correction:** implement a demonstrably native streaming route,
including buffer queueing/dequeueing and cleanup, or otherwise reconcile the
library contract without silently adding image conditioning. Add a real-library
streaming-only integration regression and physical UVC verification.

## BOUNDARY-02 — Exact-sized assessment buffers expose out-of-bounds JSON reads

**Priority:** Medium, trusted-process memory safety. **Tracker:** TD-008.
**Confidence:** High; reproduced with guard pages against the unchanged parser.

`EntropyFile::Read` (`src/entropy_process.cpp:31–42`) returns exactly the file size
in a byte vector. `ParseAssessment` (lines 119–130) gives that non-NUL-terminated
span to Core 31.1's UniValue. The pinned tokenizer's `getJsonToken` numeric and
keyword lookaheads can read beyond the supplied logical end. Parsing also occurs
before rejection of an unsuccessful worker exit status.

The probe placed a report ending in `-`, `0`, `n`, `t` or `f` immediately before
an inaccessible page. For **both worker status 0 and status 1**, all ten exact-span
cases terminated the child with **SIGSEGV**. The same ten logical spans with one
additional backing NUL byte rejected normally. For example:

```text
token=- worker_exit=0 sentinel=0 signal=11 exit=-1
token=- worker_exit=0 sentinel=1 signal=0 exit=0
token=- worker_exit=1 sentinel=0 signal=11 exit=-1
token=- worker_exit=1 sentinel=1 signal=0 exit=0
```

**Prerequisite:** malformed/truncated report bytes from a malfunctioning or
compromised assessment worker. The normal complete pinned report does not trigger
this example. A particular production allocation need not fault immediately;
the guard-page fixture establishes the out-of-bounds access.

**Effect:** The fault is in the signer, outside the assessor's confinement. No
disclosure or code execution was demonstrated. This path normally runs before
the final RNG request and manual input, which limits the immediate secret state;
that timing does not justify unsafe parsing in the trusted process.

**Recommended correction:** satisfy the tokenizer's backing-storage requirements
or fix its end checks, preserving the logical length. A NUL-backed representation
rejects these fixtures, but should be reviewed against all tokenizer lookaheads.
Reject irrelevant failed-worker statuses before parsing, while retaining explicit
constant-alphabet handling. Test exact-size, non-string-backed malformed input.

## BOUNDARY-03 — Address-space limits do not bound anonymous-file memory

**Priority:** Medium, conditional denial of service. **Tracker:** TD-009.
**Confidence:** High for the accounting gap; device-level OOM impact not tested.

The camera reuses `ConfineScanner` (`src/isolation.cpp:50–79`), which limits
virtual address space, open descriptors and additional processes. It does not
restrict memfd creation/writes or set `RLIMIT_FSIZE`. Landlock retains access to
anonymous files; writing their backing pages does not require mapping them into
the worker's virtual address space. The parent pipe cap does not cover such writes.

A bounded probe applied the real camera policy, then lowered the address-space
limit to 32 MiB. With a small existing buffer it wrote and released one 40 MiB
memfd, inside a container capped at 128 MiB with swap disabled:

```text
address_space_limit=33554432 memfd_bytes=41943040 allocated_bytes=41943040
```

**Prerequisite:** code execution inside the camera worker; no decoder exploit was
demonstrated. The result illustrates a resource-accounting mechanism rather than
an attempted OOM. A compromised worker could exert memory pressure beyond the
advertised address-space limit. This policy limitation also affects the older
scanner and is not newly introduced solely by generation.

**Recommended correction:** describe the limit specifically as virtual address
space. Restrict unnecessary anonymous-file allocation/writing or use accounting
that includes kernel-backed storage. A camera file-size limit would close the
demonstrated write route, but is not comprehensive kernel-memory accounting. The
assessment policy already has a file-size limit; do not infer from this probe
that all other kernel-memory routes have been bounded or reviewed.

## Lower-priority source findings

These were confirmed by source inspection, not new physical-device experiments.

### BOUNDARY-04 — Report shape is weaker than the complete-battery claim

**Low; TD-010.** `src/entropy_process.cpp:130–144` requires eleven objects and one
valid `Overall`, but not the other ten expected case identities or appropriate
result fields. The existing success test at `tests/entropy.cpp:112–117` accepts
ten identical `Fixture method` objects with no estimates. Malformed/incompatible
worker output can therefore be labeled complete. This is diagnostic integrity,
not proof of weakened final randomness. Check the expected identities and valid
conditional fields, or narrow the claim. Even a stricter schema cannot prove a
compromised worker really executed the estimators.

### BOUNDARY-05 — Invalid preferred geometry prevents same-device fallback

**Low; TD-011.** In `src/camera.cpp:59–86`, the format loop stops after successful
`S_FMT` and matching fourcc. Geometry is checked afterward. If preferred GREY
negotiates an unsupported size, the entire device is rejected without trying a
potentially valid YUYV/RGB format on that device. Validate each candidate fully
before ending the fixed preference search. No specific physical camera with this
combination was tested.

### BOUNDARY-06 — Twenty seconds is not a strict acquisition cutoff

**Low; TD-012.** `src/camera_entropy_main.cpp:30–60` checks time before a blocking
capture. A late-returning frame can still complete and enter a batch; a blocking
send can also extend runtime. The parent permits capture for 30 seconds
(`src/entropy_capture.cpp:37–39`). Define whether 20 seconds is an acquisition
cutoff or a soft target with startup/drain allowance, enforce that interpretation,
and document the separate watchdog. The overall deadline still exists.

### BOUNDARY-07 — Unsupported-host tests contradict documented skip behavior

**Low; TD-013.** The new entropy-confinement, entropy-assessment and successful
camera/assessment terminal cases require usable Landlock ABI 6. They lack the
older isolation suite's capability/skip handling, despite the broader skip claim
in `docs/STATUS.md:254–259` and `docs/ISOLATION.md:172–176`. On an unsupported host
they fail, not skip. Keep production fail-closed behavior; either add test-only
capability handling or document the full suite's host requirement. This host
configuration was not reproduced during the audit.

## Controls observed and limits of the review

- Fresh executable/session, fixed environment and inherited-FD closure are present.
  Input/output descriptors are duplicated above remapping destinations first.
- Exception/cancellation cleanup kills and waits for workers; reaped PIDs are
  cleared before destruction. No surviving-worker path was found in this review.
- Record allocation, cumulative bytes, dimensions and sample-prefix sizes are
  bounded. Partial records do not renew the liveness deadline.
- Native component extraction excludes padding; temporal order is position,
  component, then time within each batch. The same received bytes are hashed and
  copied unchanged into the sealed assessment prefix.
- NIST is separate from the signer; the JSON-warning patch does not alter estimator
  mathematics. Test RNG wrappers and unconfined benchmark executables are excluded
  from production. Native closure/symbol tests passed.
- Camera scores receive no guaranteed entropy credit. Neither this review nor
  statistical appearance establishes physical unpredictability.

The review did not test physical capture, every driver/control behavior, entropy
quality, old-laptop performance, complete crypto-library memory cleanup, firmware
or boot-media attacks, sustained fuzzing, or extraction of actual keys. Cancellation
also depends on the kernel eventually returning from an uninterruptible driver
wait; `SIGKILL` plus blocking reap is not a universal hardware-hang time guarantee.

## Recommended order and high-value-use decision

1. Correct the trusted JSON boundary and the cryptographic report's stack cleanup;
   add regressions that fail on this revision and pass on the correction.
2. Correct native camera streaming and exercise the real capture path; tighten
   resource claims and address the demonstrated anonymous-file route.
3. Resolve the smaller report-schema, format-fallback and deadline discrepancies,
   then validate the intended laptop/camera and independent backup recovery.
4. Re-review the corrected revision and exact artifact. The existing compact-fee
   finding TD-001 and unsigned-boot/hardware finding TD-005 remain separate concerns
   for high-value signing; generation changes do not resolve them.

Healthy mandatory randomness and the tested BIP39/mixing implementation are positive
evidence. They are not sufficient to call this whole experimental implementation
bug-free or certify custody of large holdings. All findings here refer to the
identified revision; no implementation fix was made as part of this audit.
