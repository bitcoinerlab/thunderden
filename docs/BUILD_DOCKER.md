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
- `thunderden-small.img` (smallest current payload fit, UEFI-only FAT16 best-effort)
- `thunderden-small.img.sha256`
- `thunderden.SHA256SUMS` (if produced)

By default, the script generates both outputs:

- compatibility image: fixed `64` MiB FAT32 for broad firmware support
- tiny image: smallest UEFI-only FAT16 size that still fits current payload

## Useful options

```bash
./scripts/build/docker_build_thunderden.sh --output-dir ./artifacts
./scripts/build/docker_build_thunderden.sh --buildroot-version 2025.11.1
./scripts/build/docker_build_thunderden.sh --rebuild-image
./scripts/build/docker_build_thunderden.sh --clean-cache
```

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
