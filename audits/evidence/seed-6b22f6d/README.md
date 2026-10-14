# Public seed-audit reproductions

These probes reproduce findings at `6b22f6dfb27371f0fb160e4ca8f068f72cfa335e`.
They are audit evidence, not production code or default CI tests. All inputs are
public fixtures. They require Linux/amd64, the reviewed native test build and,
for the confinement probe, Landlock ABI 6. No physical camera or wallet is used.

The original native image ID was
`sha256:81248a4e6f0827ed284b1928f505ae7ee20f32bf7482475fcc5550420ba1ec38`.
Use that local image or build service `test` from the exact reviewed source;
a later source revision may change the expected outcomes below. Keep this evidence
directory available when checking out the reviewed revision.

From the repository root, create a separate output directory and compile:

```sh
IMAGE=sha256:81248a4e6f0827ed284b1928f505ae7ee20f32bf7482475fcc5550420ba1ec38
AUDIT_SCRATCH=/tmp/seed-audit-evidence
ls -ld /tmp
mkdir -p "$AUDIT_SCRATCH"
docker run --rm --network none --read-only \
  --user "$(id -u):$(id -g)" --cap-drop ALL \
  --security-opt no-new-privileges:true --ulimit core=0:0 \
  --tmpfs /tmp:rw,nosuid,nodev,noexec,mode=1777 \
  -v "$PWD/audits/evidence/seed-6b22f6d:/evidence:ro" \
  -v "$AUDIT_SCRATCH:/audit" "$IMAGE" python3 -B /evidence/build.py
```

Run each probe in a bounded disposable container:

```sh
for PROBE in residue_probe json_boundary native_read memfd_budget; do
  docker run --rm --network none --read-only \
    --user "$(id -u):$(id -g)" --cap-drop ALL \
    --security-opt no-new-privileges:true --ulimit core=0:0 \
    --memory 128m --memory-swap 128m --pids-limit 16 \
    -v "$AUDIT_SCRATCH:/audit:ro" "$IMAGE" "/audit/$PROBE"
done
```

## Observed outcomes

- `residue_probe`: the encoder wipe is observed, but validation leaves all 16/32
  public entropy bytes in the returned hash context with zero observed hash wipes.
  This is a platform-specific abandoned-stack observation, not a portable C++
  lifetime test, an extraction method, or proof of persistence after logout.
- `json_boundary`: all ten exact-span cases end with child signal 11; adding one
  NUL outside the logical span makes all ten controls reject normally (exit 0).
  Worker exit codes 0 and 1 both reach parsing. Core dumps are disabled.
- `native_read`: `registered=1 read_result=-1 errno=22 raw_read_calls=1 streaming_setup_calls=0`.
  The real libv4l2 API is used; only the device syscall interface is synthetic.
- `memfd_budget`: `address_space_limit=33554432 memfd_bytes=41943040 allocated_bytes=41943040`.
  The production camera policy is applied, then its address-space limit is lowered
  to 32 MiB for a bounded demonstration. Only 40 MiB is written and then released.
  This checks the accounting gap without attempting OOM or touching other processes.

The parent of the JSON probe exits normally after recording child results; inspect
the printed outcomes rather than treating its overall exit status as a test pass.
The other probes expect the reviewed vulnerable behavior. After fixes, new
regression tests should assert the desired behavior rather than preserve a flaw.
