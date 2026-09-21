# Build and tests

Requires Docker with Compose and Linux-container support. Docker runs the build
tools on the host's native architecture: ARM64 on Apple Silicon or AMD64 on
Intel/AMD machines. Buildroot cross-compiles the USB image for x86-64 laptops.
Development tests run on the builder's architecture.

The Debian image digest in `.env` pins a multi-architecture index containing the
base images for both architectures. Use Docker's default platform selection;
leave `DOCKER_DEFAULT_PLATFORM` unset.

```text
docker compose run --build --rm test
```

The development build compiles the native Core-backed application and runs its
tests as uid/gid 1000 in a container with networking and capabilities disabled,
a read-only root filesystem and temporary writable test directories. Current
verification coverage is listed in [STATUS.md](STATUS.md).

Source versions, hashes, the base-image digest and Debian snapshot are defined in
`.env`. Build products remain inside Docker. A CMake build can also use an existing
verified Core source tree by supplying `CORE_SOURCE_DIR`, `UR_SOURCE_DIR` and `BIP39_WORDLIST`.
The `BIP39_WORDLIST_SHA256` setting must match the pinned wordlist hash.

The CTest suites cover seed/policy handling, transaction review/signing, Core key
vectors, native dependency/syscall checks, UR transport, application requests,
independent account-export compatibility, terminal interaction and camera startup.
Signing tests use public deterministic fixtures and synthetic previous transactions.
Linker wrappers count ECDSA/Schnorr calls to check that review and rejection do
not sign.

The `isolation` suite checks scanner containment and communication with the signer.
Landlock enforcement tests require ABI 6 support in the host kernel (normally
Linux 6.12 or later with Landlock enabled). That suite reports a skip on unsupported
hosts; the scanner itself refuses to scan without confinement. Image construction
uses only Docker and enables Landlock in the pinned guest kernel. See
[Scanner isolation](ISOLATION.md) for the security boundary.

To see individual checks, dependency details and test-executable size measurements
after building:

```text
docker compose run --rm test ctest --test-dir /build --output-on-failure -V
```

`thunderden-signer` and `thunderden-scanner` are the application targets. Set
`TD_BUILD_TESTS=OFF` for the image build; test executables are not installed.
Reported test-executable sizes include fixtures and are not production application
or image sizes.

## Boot image

```text
docker compose run --build --name thunderden-image-build image
docker cp thunderden-image-build:/cache/out/images/thunderden.img .
docker cp thunderden-image-build:/cache/out/images/thunderden.img.sha256 .
docker cp thunderden-image-build:/cache/out/images/installed-files.json .
docker rm thunderden-image-build
```

The build checks the Buildroot archive hash and its signed checksum announcement,
then builds the native application with the Buildroot toolchain. The 64 MiB image
contains a FAT32 boot partition, legacy BIOS GRUB, 64-bit UEFI GRUB and Linux with
an embedded RAM filesystem. Image assembly needs no loop devices or privileged
container. It fixes disk/FAT identifiers, insertion order and FAT timestamps.
The build inventories the packed initramfs and checks the resolved kernel options
and installed files before assembling the disk image.

Downloads are cached in Docker volumes; image products reside in `thunderden-v2-out`.
Changing the builder architecture or platform/toolchain configuration requires a
clean output volume. Cached build tools cannot be shared between AMD64 and ARM64.
Reassembling identical payloads tests disk metadata determinism. Complete
clean-build verification is described below; recorded results are in
[Implementation status](STATUS.md).

`--build` does not clear the output cache. To clear it, first remove stopped
containers using `thunderden-v2-out` (for example, `docker rm thunderden-image-build`),
then run `docker volume rm thunderden-v2-out` and repeat the build command above.
This removes generated build outputs; downloaded sources remain cached.

## Clean-build comparison

A clean comparison rebuilds the toolchain, libraries, bootloader, kernel and
application. Docker's `--no-cache` rebuilds the builder but does not empty Buildroot's
Docker volumes, so each run needs its own fresh volumes too.

Use two clean checkouts of the same commit with identical `.env` pins. In a POSIX
shell, run the following in the first checkout, then repeat in the second with
`run=thunderden-repro-b`. The container name and all three volume names must be
unused; reusing an output volume would make this an incremental build.

```sh
run=thunderden-repro-a
docker compose build --no-cache image
docker compose run --name "$run" \
  -v "${run}-src:/cache/src" \
  -v "${run}-dl:/cache/dl" \
  -v "${run}-out:/cache/out" image
```

Both builds must finish successfully, including the installed-file and disk-assembly
checks. They retain their outputs and logs in the named volumes and containers.
Compare their artifacts using the builder's Python interpreter:

```sh
docker compose run --rm \
  -v thunderden-repro-a-out:/repro-a:ro \
  -v thunderden-repro-b-out:/repro-b:ro \
  image python3 /work/tests/compare_builds.py /repro-a/images /repro-b/images
```

The comparison requires byte-identical disk images, kernel/initramfs, BIOS/UEFI
boot payloads, checksum files and installed-file inventories. It reports each
artifact's size and SHA-256 and identifies changed inventory entries on failure.
Keep the source commit, pins, host/platform details, logs and comparison output
with the verification record. Results from another machine provide additional
independent confirmation. These checks use Docker and do not require QEMU.

## Optional developer boot/display checks

QEMU is not needed to build the image, run the default test suites or audit the
build configuration and generated artifacts. It is an optional developer tool
for exercising boot and display behavior and is not installed in the signer image.

On a Linux host with QEMU and the development test image built:

```text
python3 tests/boot.py thunderden.img /tmp/thunderden-bios
python3 tests/boot.py thunderden.img /tmp/thunderden-uefi --firmware /path/to/OVMF.fd
```

The driver enters a public test mnemonic through the booted UI, reviews an account
export, captures its framebuffer QR and checks the decoded account against the
expected fixture. Screenshots and QEMU logs are kept in the specified directory.
This checks boot/display behavior; it does not exercise a physical webcam.
The `--firmware` argument expects a combined OVMF firmware image.

The optional isolation-on-guest check uses a separate test binary/overlay, never
installed into the image. First complete the boot-image build above, then build
the test binary using the same output volume and its image toolchain (JPEG ABIs
can differ from the development container):

```text
docker compose run --name thunderden-guest-tests image sh /work/tests/build_guest_tests.sh
docker cp thunderden-guest-tests:/cache/out/images/bzImage /tmp/bzImage
docker cp thunderden-guest-tests:/cache/out/validation/isolation-tests /tmp/isolation-tests
docker rm thunderden-guest-tests
python3 tests/boot_isolation.py /tmp/bzImage /tmp/isolation-tests
```

This developer-only check additionally uses the host `cpio` utility. It is optional
for the same reason as the boot/display checks above.
