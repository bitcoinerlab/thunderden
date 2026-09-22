# Build an image and boot from USB

## TL;DR: build, flash, boot

You need Docker with Compose and Linux-container support, a USB drive and an
x86-64 laptop to boot it. Run the commands from the repository folder, with
Docker running.

### 1. Build the image

```sh
docker compose run --build --name thunderden-image-build image
```

This builds the signer and its bootable Linux system. The first build also compiles
the toolchain and dependencies, so allow plenty of time. Later builds reuse caches.

Once it finishes successfully, copy the image and its checksum to your current
folder, then remove the finished container:

```sh
docker cp thunderden-image-build:/cache/out/images/thunderden.img .
docker cp thunderden-image-build:/cache/out/images/thunderden.img.sha256 .
docker rm thunderden-image-build
```

Check the copied image against its checksum:

- **Linux:** `sha256sum -c thunderden.img.sha256`
- **macOS:** `shasum -a 256 -c thunderden.img.sha256`
- **Windows PowerShell:** `Get-FileHash .\thunderden.img -Algorithm SHA256`;
  compare the result with the hash in `thunderden.img.sha256`.

### 2. Write it to a USB drive

Use an image writer such as [balenaEtcher](https://etcher.balena.io/):
**Flash from file → `thunderden.img` → Select target → your USB drive → Flash**.
Wait for writing and verification to finish, then eject the drive.

**Writing the image erases the selected drive. Double-check the USB you select.**
The image is only 64 MiB, so a small boot partition on a larger USB is normal.
For a terminal-only method, see [Write the USB from Linux](#write-the-usb-from-linux).

### 3. Boot the laptop

Insert the USB before turning the laptop on. Open its boot menu and select the
**UEFI USB** entry. The same image also supports legacy BIOS boot.
The development image is unsigned, so Secure Boot must be disabled for UEFI boot.
Thunder Den starts at network selection. Use test networks and test recovery words
while the project is under development.

## Build or test: which command do I need?

| Command | What it does |
| --- | --- |
| `docker compose run --build --name thunderden-image-build image` | Builds the bootable USB image for an x86-64 laptop. |
| `docker compose run --build --rm test` | Builds the application and runs automated software tests inside Docker. It does not create a USB image. |

`--build` prepares or updates the Docker build environment first, using its cache.
`--rm` removes the container after it exits. We keep the image-build container
until the `docker cp` commands have retrieved its files.

Docker uses native build tools: ARM64 on Apple Silicon or AMD64 on Intel/AMD.
The USB image always targets x86-64. Leave `DOCKER_DEFAULT_PLATFORM` unset.
Source versions and checksums are pinned in `.env`.

The image build checks source integrity, kernel settings, installed files and
repeatable disk-image assembly. It also creates `installed-files.json`, a list of
the files and hashes inside the boot system. To save it, run this before removing
the build container:

```sh
docker cp thunderden-image-build:/cache/out/images/installed-files.json .
```

## Write the USB from Linux

First identify the USB by its size and model:

```sh
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
```

In the commands below, replace `/dev/sdX` with the **whole USB device**, such as
`/dev/sdb`, not a partition such as `/dev/sdb1`. Unmount any mounted partitions
listed for that USB first, for example with `sudo umount /dev/sdb1`.
**The write command overwrites the selected drive.**

```sh
sudo dd if=thunderden.img of=/dev/sdX bs=4M conv=fsync status=progress
sudo cmp -n "$(stat -c %s thunderden.img)" thunderden.img /dev/sdX
```

`dd` writes the image and flushes it to the drive. `cmp` compares the written
portion with the image; no output means they match. Eject the USB after both
commands finish successfully.

## Rebuilding and managing the cache

For application changes, repeat the quick-start build and copy commands.
Downloads and build outputs stay in Docker volumes when you remove a container.

- **Container name already in use?** Inspect its log with
  `docker logs thunderden-image-build`. Once it has stopped, remove it with
  `docker rm thunderden-image-build` and retry.
- **Changed builder architecture or platform/toolchain settings?** Remove stopped
  containers using the output volume, then run `docker volume rm thunderden-v2-out`
  and rebuild. This clears compiled outputs but keeps downloaded sources.

`--build` does not clear those volumes. AMD64 and ARM64 builders need separate
output caches.

## Run the software tests

```sh
docker compose run --build --rm test
```

Use this when developing or checking the application. The tests cover key
derivation, wallet policies, signing, QR exchange, the terminal interface and
scanner isolation. They use public test data and simulated devices; no USB drive
or webcam is needed. Test programs are not installed in the boot image.

The tests run in a restricted container. Some isolation checks may be skipped
when the host kernel lacks Landlock ABI 6 support, normally available with
Landlock enabled on Linux 6.12 or later. The USB image includes its own kernel
with that support. See [STATUS.md](STATUS.md) for coverage and physical-hardware
checks still to do.

After building the test image, rerun the same compiled tests with detailed output:

```sh
docker compose run --rm test ctest --test-dir /build --output-on-failure -V
```

## Clean-build comparison

<details>
<summary>Optional: check whether two independent builds produce identical files</summary>

This is much slower than a cached rebuild. Use two clean checkouts of the same
commit with identical `.env` pins. In a POSIX shell, run this in the first checkout,
then repeat in the second with `run=thunderden-repro-b`. Each container and volume
name must be unused.

```sh
run=thunderden-repro-a
docker compose build --no-cache image
docker compose run --name "$run" \
  -v "${run}-src:/cache/src" \
  -v "${run}-dl:/cache/dl" \
  -v "${run}-out:/cache/out" image
```

`--no-cache` rebuilds the Docker environment. The three fresh volumes also force
fresh source downloads and a full toolchain, kernel and application build.
After both builds succeed, compare their outputs:

```sh
docker compose run --rm \
  -v thunderden-repro-a-out:/repro-a:ro \
  -v thunderden-repro-b-out:/repro-b:ro \
  image python3 /work/tests/compare_builds.py /repro-a/images /repro-b/images
```

This checks that the disk image, kernel, bootloaders and installed-file inventories
match byte-for-byte. Keep the commit, pins, build-machine details, logs and result
with your verification record. Recorded comparisons are in [STATUS.md](STATUS.md).

</details>

## Optional developer boot/display checks

<details>
<summary>Boot the image in QEMU without using a physical laptop</summary>

QEMU is optional; ordinary builds and software tests need only Docker. These
commands require a Linux host with QEMU, a built USB image and the Docker test
image from the software-test command above.

```sh
python3 tests/boot.py thunderden.img /tmp/thunderden-bios
python3 tests/boot.py thunderden.img /tmp/thunderden-uefi --firmware /path/to/OVMF.fd
```

The first command tests BIOS boot; the second tests UEFI boot using a combined
OVMF firmware file. Both enter a public test mnemonic and check the displayed
account QR. Screenshots and logs are saved in the chosen folders. These checks
do not test a physical webcam.

To check scanner isolation under the image's kernel, build a separate test program
with the image toolchain and boot it in QEMU. This also requires the host `cpio`
utility:

```sh
docker compose run --name thunderden-guest-tests image sh /work/tests/build_guest_tests.sh
docker cp thunderden-guest-tests:/cache/out/images/bzImage /tmp/bzImage
docker cp thunderden-guest-tests:/cache/out/validation/isolation-tests /tmp/isolation-tests
docker rm thunderden-guest-tests
python3 tests/boot_isolation.py /tmp/bzImage /tmp/isolation-tests
```

The test program is loaded in a temporary overlay, separate from the USB image.

</details>
