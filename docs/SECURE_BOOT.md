# Secure Boot: checking your USB before it runs

Before entering your recovery words, you want to know that your laptop is running
the Thunder Den software you meant to boot. Checking a download's checksum is a
good start. But what if someone changes the USB afterwards?

Secure Boot lets the computer check the software itself, every time it starts.
It is a feature of modern laptop firmware—the software that runs before the
operating system. With Secure Boot enabled and the right keys installed, the
firmware checks a digital signature before allowing Thunder Den to run.

Think of the signature as a tamper-evident seal. The person signing the image
uses a **private key** to make it. Your laptop uses the matching **public key**
to check it. If someone changes the signed software, that check fails and the
computer refuses to load it.

Thunder Den signs the parts that become the running system: the Linux kernel
and the whole small system packed inside it, including Thunder Den, its
libraries, startup scripts and settings. This protects more than the application
alone: a replacement operating system could otherwise show a convincing fake
Thunder Den screen. The protected boot path also ignores outside startup settings
and extra system files that could replace this system.

*Signed images and public signing material are available from
[v0.0.1-preview.12](https://github.com/bitcoinerlab/thunderden/releases/tag/v0.0.1-preview.12).
Signing was first included in preview 9; previews 8 and earlier are unsigned.
Disposable test certificates are not release keys.*

Thunder Den's release-signing certificate SHA-256 fingerprint is:

```text
77:C2:64:BD:D1:E9:7B:E9:E9:9C:15:6E:90:85:8E:45:C8:B6:1E:FD:CA:23:22:6D:1A:CF:87:E3:6B:37:49:16
```

Confirm it through a trusted channel before enrollment, rather than trusting a
certificate merely because it arrived on the USB you are checking.

## What you still trust

Secure Boot reduces the need to trust that the USB has remained untouched. It
does **not** remove the need to trust your computer's firmware and hardware.
The firmware performs the check; compromised firmware could simply skip it.
Nor does a valid signature prove that the signed software is safe or bug-free.
You still need to decide which software you want to run in the first place.

The signature does not cover every byte of the USB. It covers the system that
runs, not unused space or every piece of disk metadata. Changes elsewhere may
make the USB stop booting, without being a signature failure. Secure Boot is
also a UEFI feature: booting through legacy BIOS does not provide this protection.

## Use a published image without taking the builder's word for it

You can use an image signed with Thunder Den's release key without simply
trusting the project's build. Thunder Den's unsigned build is reproducible, so
you can build the same release yourself and compare the results.

There is one wrinkle: your normal build has no signature, so it will not be
identical to the signed download. That is why a signed release also includes
the unsigned image and the signature as a separate public file.

First, compare your build with the published unsigned image. Then use the
published signature to reconstruct the signed image from your own build. The
verification tool checks that this reconstruction matches the signed download
**byte for byte**. You do not need Thunder Den's private signing key to do this.

If both checks pass, the signed download contains the same software as your own
build, with the public signature attached. For that image, you have checked the
builder's work rather than taken his word for it. You still rely on the source
you chose, your build tools and the computer doing the verification.

Your firmware can then check Thunder Den's signature whenever you boot. Remember
that trusting the release key also allows other images signed with that key—not
just the release you checked, and potentially older releases too. Verify each update.
If you want to control which builds receive a valid signature yourself, use
your own key instead.

## Or build it and sign it yourself

You can build Thunder Den and sign it with your own private key. Signing happens
after the build; it does not recompile or change the software. You then tell
your laptop to accept your public key rather than Thunder Den's release key.

Keep the private key on your trusted signing computer, encrypted with a password.
Only the public key, normally carried in a file called a **certificate**, belongs
in the laptop's firmware. Neither key has anything to do with your Bitcoin wallet:
you never need recovery words to build, sign or check an image.

The example below creates a certificate valid for roughly **100 years**, to suit
long-term storage. That is not a promise that today's USB, laptop or cryptography
will last a century. Your recovery words remain the essential backup, independent
of the boot image and its signing key.

## Want help setting this up?

ChatGPT, Claude or another AI assistant can guide you through the whole process:
choosing a published signature or your own key, building and checking an image,
signing it, and adding the public certificate to your laptop. You can start with:

> Using this Thunder Den Secure Boot guide, walk me through setup. My computer
> runs [operating system]; my laptop is [model], with firmware [version], and also
> boots [other operating systems, or none]. Help me choose Thunder Den's release
> signature or my own key, and explain each step. For my own signing key, use an
> encrypted private key and a 100-year certificate. Give me key-creation and signing
> commands to run myself; never ask for my private key or password. Check the
> manufacturer's instructions for this exact device. Establish whether enrollment
> adds a certificate or replaces existing trust, and how to keep my other operating
> systems bootable and accessible. Check whether a BIOS administrator password can
> protect Secure Boot settings and what happens if I forget it; never ask for that
> password. Ask before any firmware changes; do not guess when the procedure is unclear.

Use the AI for guidance, not for handling secrets. Create and use your private
key yourself in your terminal, entering its password only at the tool's prompt.
Public certificates are fine to share. The manual build, signing and verification
steps are below; firmware enrollment still needs device-specific instructions.

## Adding the public key to your laptop

The goal is to **enroll the image's public signing certificate in your laptop's
Secure Boot firmware settings**—Thunder Den's release certificate or your own.
This tells the firmware, “allow software signed by this key.”

Often, you copy the public certificate or prepared import files to a USB and
select them through the firmware's key-management menu. This is called
**enrollment**. The firmware remembers the public key afterwards; you normally
enroll it once, not again for every release signed with the same key. Your
private key never belongs on that USB or in the laptop's firmware.

**If this laptop also boots Windows, Linux or other operating systems, changing
Secure Boot settings or keys can stop them booting or trigger disk-encryption
recovery prompts.** Some firmware can add one certificate; other import procedures
replace existing trust. Do not assume that “Import” means “add.”

The menus, file formats and precautions depend on the exact model, firmware and
installed systems. This guide is not a universal enrollment or recovery manual.
Work out the appropriate procedure using the manufacturer's instructions,
optionally with an AI assistant's help as above. Before changing anything, make
sure the plan accounts for your other operating systems and recovery options;
do not blindly clear or replace keys.

If using Thunder Den's release certificate, confirm its fingerprint through a
trusted channel—not just a certificate found on the USB you are checking.

Once enrollment has succeeded, enable Secure Boot,
[write the signed image to USB](BUILD.md#2-write-it-to-a-usb-drive) and select its
UEFI boot entry. Both signed and unsigned images can also boot with Secure Boot
disabled, but then the firmware is not checking this seal.

Test both sides: the signed image should boot, and the unsigned one
should be refused. Use test recovery words for these checks. Secure Boot must
actually be enforcing signatures, with no legacy-boot bypass. Other trusted
keys, such as factory-installed ones, may still allow other operating systems.
The protection depends on these settings remaining under your control.

## Protecting the firmware settings

Secure Boot is easy to bypass if someone can simply turn it off or replace its
trusted keys. Where supported, we recommend a **BIOS/UEFI administrator (setup)
password**. Check that it protects Secure Boot changes and key management on your
model. This is not your operating-system PIN or a power-on/disk password.

**Check the manufacturer's recovery policy before setting it.** Save the password
in a password manager with a separate secure backup; never share it with an AI.
A forgotten firmware administrator password may require hardware service or even
motherboard replacement. Restoring factory Secure Boot keys is not a password
reset.

This helps prevent casual settings changes, not all physical tampering. Keeping
the laptop physically secure and trusting its hardware and firmware still matter.

## Commands and technical details

The commands below are optional reading until you are ready to set this up.
Run them from a reviewed checkout of the release you intend to use. The build
and public verification tools use Docker; manual signing uses OpenSSL and
`sbsign` from the Linux `sbsigntool` package on your trusted host.

Current source uses only the single-payload image format 2 (included from preview
11). To reconstruct preview 9 or 10, use the tools from that release's tag instead.
The disk-layout version is recorded in `/boot/boot-policy.json`; it is packaging
metadata, not a firmware trust decision.

<details>
<summary>Compare your build with a published signed release</summary>

[Build the same source revision](BUILD.md#1-build-the-image) and copy its unsigned
image to `out/latest/`. Put the release's two images and their `.sha256` files in
`out/download/`, along with `thunderden-boot.pk7`, `thunderden-signing.pem` and
`secure-boot.json`. The `.pk7` is the detached EFI signature; the `.pem` is public.

[Check both downloaded checksums](BUILD.md#verify-the-image), using
`thunderden-signed.img.sha256` for the signed image. Then compare and reconstruct:

```sh
cmp out/latest/thunderden.img out/download/thunderden.img
docker compose run --build --rm boot-tools python3 -B /work/build/secure_boot.py verify \
  /out/latest/thunderden.img /out/download/thunderden-signed.img \
  /out/download/thunderden-boot.pk7 /out/download/thunderden-signing.pem \
  --fingerprint YOUR_TRUSTED_CERTIFICATE_SHA256
```

Replace the placeholder with the independently confirmed certificate SHA-256
fingerprint: 64 hexadecimal digits, with or without colons. This is not a Bitcoin
wallet fingerprint. `cmp` produces no output on success; the verification tool
reports an exact signed-image match. Stop if either check fails.

The tool reconstructs EFI headers and disk-filesystem metadata as well as
attaching the signature. It does not ignore arbitrary differences between images.
The packaging tools are pinned through the build's Debian snapshot.

</details>

<details>
<summary>Build and sign with your own key</summary>

**1. Create an encrypted signing key once.** Run these commands yourself, not
through an AI agent. Choose new paths if the files already exist: OpenSSL may
overwrite them. Enter passwords only at the tool's prompt, never in chat,
scripts, command arguments or environment variables.

```sh
umask 077
mkdir -p "$HOME/.local/share/thunderden-signing"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -aes-256-cbc \
  -out "$HOME/.local/share/thunderden-signing/release.key"
openssl req -new -x509 -sha256 -days 36525 \
  -key "$HOME/.local/share/thunderden-signing/release.key" \
  -subj '/CN=Thunder Den Release Signing/' \
  -addext 'basicConstraints=critical,CA:FALSE' \
  -addext 'keyUsage=critical,digitalSignature' \
  -addext 'extendedKeyUsage=codeSigning' \
  -out "$HOME/.local/share/thunderden-signing/release.pem"
openssl x509 -in "$HOME/.local/share/thunderden-signing/release.pem" \
  -noout -fingerprint -sha256 -dates
```

Keep the `.key` outside the repository and Docker mounts, and back it up with
its password stored separately. The `.pem` and fingerprint are public.
Encryption protects a stored key, not a compromised signing computer. Prefer
signing after the agent has stopped working.

The final command prints the certificate's fingerprint and validity dates.
`36525` days is about 100 years; this validity period belongs to the certificate,
not the key pair itself, and is not a guarantee of future cryptographic security.

**2. Build and prepare the image.** [Build and copy the unsigned image](BUILD.md#1-build-the-image)
to `out/latest/`, then extract the exact EFI executable to sign:

```sh
docker compose run --build --rm boot-tools python3 -B /work/build/secure_boot.py prepare \
  /out/latest/thunderden.img /out/latest/thunderden.efi
```

This requires an unsigned format-2 image; older layouts and already-signed EFI
payloads are rejected. Check that
the build is the one you intend to authorize, and record the reported EFI hash.

**3. Sign it yourself on the host**, not through an agent or a key-mounted container:

```sh
sbsign --detached \
  --key "$HOME/.local/share/thunderden-signing/release.key" \
  --cert "$HOME/.local/share/thunderden-signing/release.pem" \
  --output out/latest/thunderden-boot.pk7 out/latest/thunderden.efi
cp "$HOME/.local/share/thunderden-signing/release.pem" out/latest/thunderden-signing.pem
```

Enter your password at the prompt. Do not make a decrypted key file for Docker.

**4. Assemble the signed image.** Use your certificate's fingerprint from step 1
and choose a new output directory:

```sh
docker compose run --rm boot-tools python3 -B /work/build/secure_boot.py package \
  /out/latest/thunderden.img /out/latest/thunderden-boot.pk7 \
  /out/latest/thunderden-signing.pem /out/release-pair \
  --fingerprint YOUR_TRUSTED_CERTIFICATE_SHA256
```

This creates `out/release-pair/thunderden-signed.img`, the unsigned image,
checksums and public signing files. Run the reconstruction command above with
`/out/release-pair/` paths to check the pair, then flash the **signed** image.
The developer release process is the same, using the release key instead of a
personal key. A release must include both images and public signing material,
the matching `installed-files.json`, and its source revision.

</details>

<details>
<summary>Firmware terminology and certificate formats</summary>

The allowed-signature database, **`db`**, is where firmware trusts image-signing
certificates. **`PK`** and **`KEK`** manage ownership and updates to firmware
trust; they have a different role. Trusting Thunder Den's release certificate
belongs in `db`; do not install it as your `PK` or `KEK` and give the project
control of your firmware trust policy.

If your firmware requires custom owner-management keys, use separate keys that
you control, not the image-signing key. Keep device-specific enrollment files and
private keys outside the source checkout.

If your manufacturer's instructions request a DER certificate, convert the
**public** certificate, not the private key:

```sh
openssl x509 -in out/release-pair/thunderden-signing.pem -outform DER \
  -out out/release-pair/thunderden-signing.cer
```

Some firmware instead requires prepared EFI signature lists or signed variable
updates, sometimes named `.bin`, in a particular USB folder. These are public
enrollment files, not private keys. Renaming a certificate to `.bin` is not a
conversion. Prepare them for the exact firmware and import them before enabling
Secure Boot; check that import succeeded. **Setup Mode** is not enforced Secure Boot.

A replacement certificate does not revoke an old one. If a signing key is
compromised, remove or revoke its trust on affected laptops. Thunder Den does
not enforce a minimum release version: older validly signed software may boot.

The protected executable is a directly booted EFI Linux kernel with its complete
built-in initramfs. `CONFIG_CMDLINE_OVERRIDE` fixes its command line; built-in
`noinitrd` disables the EFI stub's external initramfs loaders. BIOS GRUB remains
a separate unauthenticated path, loading that same EFI kernel through the Linux
boot protocol without verifying its EFI signature. Sharing the file does not
make BIOS authenticated. Neither signature checking nor byte-for-byte
reconstruction makes compromised firmware trustworthy.

</details>

## Developer checks

After a build, run packaging regressions and firmware tests:

```sh
docker compose run --build --rm boot-tools python3 -B /work/tests/secure_boot.py /out/latest/thunderden.img
docker compose run --build --rm boot-tests python3 -B /work/tests/secure_boot_vm.py \
  /out/latest/thunderden.img /out/secure-boot-checks
```

The VM test uses pinned QEMU/OVMF and temporary disposable keys. It writes public
evidence to a new directory, never changes host firmware, and deletes its private
test keys. It also checks a 100-year certificate with the VM clock about 30 years
ahead; this tests date handling, not future hardware or cryptographic security.
Its x86-64 tools run under emulation on ARM hosts and can be slow.
**Never enroll its test certificate or distribute its test-signed image
as a production release.** Native suites, BIOS/UEFI workflow tests, clean-build
comparison and physical-device testing remain separate checks.
