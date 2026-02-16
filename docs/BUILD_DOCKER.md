# Docker Build Flow (macOS / Linux / Windows)

This flow builds Thunder Den inside Docker without compiling on a host-mounted
source directory. It avoids the most crash-prone file-sharing path on Docker
Desktop.

## Requirements

- Docker Engine or Docker Desktop with Linux containers enabled
- A shell with `bash` and `tar`
  - macOS: Terminal
  - Linux: shell of choice
  - Windows: WSL2 or Git Bash

## One-command build

From repository root:

```bash
./scripts/docker_build_thunderden.sh
```

Artifacts are written to repository root by default:

- `thunderden-uefi.img`
- `thunderden-uefi.img.sha256`
- `thunderden.SHA256SUMS` (if produced)

## Useful options

```bash
./scripts/docker_build_thunderden.sh --output-dir ./artifacts
./scripts/docker_build_thunderden.sh --buildroot-version 2025.02.10
./scripts/docker_build_thunderden.sh --rebuild-image
./scripts/docker_build_thunderden.sh --clean-cache
```

## Why this script is safer on Desktop hosts

- Source is copied into container-local filesystem before build.
- Buildroot compile workload does not run on host bind mounts.
- Download, source, and Buildroot output caches are preserved in Docker
  volumes across runs for incremental rebuilds.
- Build runs as a non-root user inside container to avoid host-tar configure
  failures.

## Notes

- If Docker Desktop offers file-sharing backend choice, prefer `VirtioFS`.
- This flow does not replace Linux-native reproducible build path in
  `docs/BUILD_IMAGE.md`; it is an additional convenience path.
- Use `--clean-cache` when you need a fully clean rebuild.
