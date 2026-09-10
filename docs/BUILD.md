# Build and tests

Requires Docker with Compose and Linux-container support.

```text
docker compose run --build --rm test
```

The development build compiles the native Core-backed application and runs its
tests. Current verification coverage is listed in [STATUS.md](STATUS.md).

Source versions, hashes, the base-image digest, and Debian snapshot are defined in
`.env`. Build products remain inside Docker. A CMake build can also use an existing
verified Core source tree by supplying `CORE_SOURCE_DIR`, `UR_SOURCE_DIR`, and `BIP39_WORDLIST`.
The `BIP39_WORDLIST_SHA256` setting must match the pinned wordlist hash.

The CTest suites cover seed/policy handling, transaction review/signing, Core key
vectors, native dependency/syscall checks, UR transport, application requests,
independent account-export compatibility, and terminal interaction. Signing tests use
public deterministic fixtures and synthetic previous transactions. Linker wrappers
count ECDSA/Schnorr calls to check that review and rejection do not sign.

To see individual checks, dependency details, and test-executable size measurements
after building:

```text
docker compose run --rm test ctest --test-dir /build --output-on-failure -V
```

`thunderden-signer` is the application target. Set `TD_BUILD_TESTS=OFF` for the
image build; test executables are not installed. Reported test-executable sizes
include fixtures and are not production signer or image sizes.

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
contains a FAT32 boot partition, legacy BIOS GRUB, 64-bit UEFI GRUB, and Linux with
an embedded RAM filesystem. Image assembly needs no loop devices or privileged
container. It fixes disk/FAT identifiers, insertion order, and FAT timestamps.
The build inventories the packed initramfs and checks the resolved kernel options
and installed files before assembling the disk image.

Downloads are cached in Docker volumes; image products reside in `thunderden-v2-out`.
Platform/toolchain configuration changes require a clean output volume for reliable
rebuilds. Reassembling identical payloads tests disk metadata determinism; full
release reproducibility requires comparing independent clean builds as well.

## Optional developer boot/display checks

QEMU is not needed to build the image, run the default test suites, or audit the
build configuration and generated artifacts. It is an optional developer tool
for exercising boot and display behavior and is not installed in the signer image.

On a Linux host with QEMU and the development test image built:

```text
python3 tests/boot.py thunderden.img /tmp/thunderden-bios
python3 tests/boot.py thunderden.img /tmp/thunderden-uefi --firmware /path/to/OVMF.fd
```

The driver enters a public test mnemonic through the booted UI, reviews an account
export, captures its framebuffer QR, and checks the decoded account against the
expected fixture. Screenshots and QEMU logs are kept in the specified directory.
This checks boot/display behavior; it does not exercise a physical webcam.
