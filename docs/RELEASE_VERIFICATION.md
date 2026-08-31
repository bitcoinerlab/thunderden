# Prebuilt Image Verification (Default User Path)

Thunder Den default user flow is:

1. Download prebuilt image.
2. Verify release signature and hashes.
3. Flash USB.

## Expected release artifacts

- `thunderden.img`
- `thunderden-small.img` (alternative x86_64 UEFI-only image)
- `SHA256SUMS`
- `SHA256SUMS.asc` (GPG detached signature)
- `thunderden-release-key.asc`

## User verification flow

```bash
mkdir -p thunderden-release && cd thunderden-release

# Replace RELEASE_BASE_URL with the release endpoint
RELEASE_BASE_URL="https://example.com/thunderden/v1.0.0"

curl -LO "${RELEASE_BASE_URL}/thunderden.img"
curl -LO "${RELEASE_BASE_URL}/thunderden-small.img"
curl -LO "${RELEASE_BASE_URL}/SHA256SUMS"
curl -LO "${RELEASE_BASE_URL}/SHA256SUMS.asc"
curl -LO "${RELEASE_BASE_URL}/thunderden-release-key.asc"

gpg --import thunderden-release-key.asc
gpg --fingerprint --keyid-format long
gpg --verify SHA256SUMS.asc SHA256SUMS

grep ' thunderden.img$' SHA256SUMS | sha256sum -c -
grep ' thunderden-small.img$' SHA256SUMS | sha256sum -c -
```

Before trusting the key, compare the release key fingerprint against at
least two independent channels controlled by the project.

## Flash verified image

```bash
sudo dd if=thunderden.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

Replace `/dev/sdX` with the real USB device.

The small image is not the default. Use it only for x86_64 UEFI systems with
Secure Boot disabled when the boot media cannot hold the 64 MiB image. It does
not boot legacy BIOS or 32-bit UEFI systems.

## Maintainer release signing flow

```bash
sha256sum thunderden.img thunderden-small.img > SHA256SUMS
gpg --armor --detach-sign --output SHA256SUMS.asc SHA256SUMS
```

Optional but recommended: publish a signed build manifest containing pinned
source versions and commit hashes.
