# Docker Build Flow (macOS / Linux / Windows WSL2)

This flow builds Thunder Den inside Docker without compiling on a host-mounted
source directory. It avoids the most crash-prone file-sharing path on Docker
Desktop.

## Requirements

- Docker Engine or Docker Desktop with Linux containers enabled
- A shell with `bash` and `tar`
  - macOS: Terminal
  - Linux: shell of choice
  - Windows: WSL2 shell (Ubuntu/Debian/etc.)

Windows note:

- Git Bash / MSYS shells are not an officially supported path for this script.
- Run from WSL2 with Docker Desktop WSL integration enabled.

## One-command build

From repository root:

```bash
./scripts/build/docker_build_thunderden.sh
```

Artifacts are written to repository root by default:

- `thunderden.img` (max-compat BIOS+UEFI, FAT32, 64 MiB)
- `thunderden.img.sha256`
- `thunderden-small.img` (minimum-size x86_64 UEFI, FAT16, no GRUB)
- `thunderden-small.img.sha256`
- `thunderden.SHA256SUMS` (if produced)

Use `thunderden.img` by default. The small image is only for x86_64 UEFI
systems with Secure Boot disabled and boot media that cannot hold 64 MiB. It
does not support legacy BIOS or 32-bit UEFI.

## Useful options

```bash
./scripts/build/docker_build_thunderden.sh --output-dir ./artifacts
./scripts/build/docker_build_thunderden.sh --rebuild-image
./scripts/build/docker_build_thunderden.sh --clean-cache
```

## Runtime vector tests

After completing at least one Docker build, test the current runtime scripts
against the generated Buildroot rootfs:

```bash
./scripts/build/test_runtime_vectors.sh
```

The harness reuses the `thunderden-out` cache and overlays the current signer,
descriptor exporter, and QR renderer scripts. It does not rebuild Buildroot or
modify the cached output.

## Why this script is safer on Desktop hosts

- Source is copied into container-local filesystem before build.
- Buildroot compile workload does not run on host bind mounts.
- Download, source, and Buildroot output caches are preserved in Docker
  volumes across runs for incremental rebuilds.
- Build runs as a non-root user inside container to avoid host-tar configure
  failures.
- Final image assembly is rootless (no privileged mode required).

## Notes

- If Docker Desktop offers file-sharing backend choice, prefer `VirtioFS`.
- This flow does not replace Linux-native reproducible build path in
  `docs/BUILD_IMAGE.md`; it is an additional convenience path.
- Use `--clean-cache` when you need a fully clean rebuild.
