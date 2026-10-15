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

*This support is currently unreleased. Preview 8 is unsigned, and no production
signing certificate from landabaso has been published yet. The published-image
route below describes how signed releases will work; test certificates are not
release keys.*

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

You can use an image signed by landabaso without simply trusting that he built
it from the published source. Thunder Den's unsigned build is reproducible, so
you can build the same release yourself and compare the results.

There is one wrinkle: your normal build has no signature, so it will not be
identical to the signed download. That is why a signed release also includes
the unsigned image and the signature as a separate public file.

First, compare your build with the published unsigned image. Then use the
published signature to reconstruct the signed image from your own build. The
verification tool checks that this reconstruction matches the signed download
**byte for byte**. You do not need landabaso's private key to do this.

If both checks pass, the signed download contains the same software as your own
build, with the public signature attached. For that image, you have checked the
builder's work rather than taken his word for it. You still rely on the source
you chose, your build tools and the computer doing the verification.

Your firmware can then check landabaso's signature whenever you boot. Remember
that trusting his key also allows other images signed with that key—not just
the release you checked, and potentially older releases too. Verify each update.
If you want to control which builds receive a valid signature yourself, use
your own key instead.

## Or build it and sign it yourself

You can build Thunder Den and sign it with your own private key. Signing happens
after the build; it does not recompile or change the software. You then tell
your laptop to accept your public key rather than relying on landabaso's.

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
> runs [operating system]; my laptop is [model], with firmware [version]. Help me
> choose landabaso's signature or my own key, and explain each step. For my own
> key, use an encrypted private key and a 100-year certificate. Give me key-creation
> and signing commands to run myself; never ask for my private key or password.
> Check the manufacturer's instructions and ask before any firmware changes.

Use the AI for guidance, not for handling secrets. Create and use your private
key yourself in your terminal, entering its password only at the tool's prompt.
Public certificates are fine to share. The complete manual steps are below if
you prefer to work without an AI.

## Adding the public key to your laptop

Whether you use your own signature or landabaso's, the laptop needs the matching
public certificate in its Secure Boot settings. Adding it tells the firmware,
“allow software signed by this key.” You do not give anyone your private key or
hand over ownership of the laptop by adding a release-signing certificate.

The exact menus and import format vary by manufacturer, model and firmware
version. Whether you follow the instructions yourself or with an AI's help,
check the manufacturer's documentation for your exact device rather than guess.

Before changing settings, save any disk-recovery keys for your existing operating
system and make sure you know how to restore the previous configuration. Do not
blindly clear the existing keys or replace the firmware's ownership keys.
If using landabaso's certificate, confirm its fingerprint through a trusted
channel—not just a certificate found on the USB you are checking.

Once the certificate is installed, [write the signed image to USB](BUILD.md#2-write-it-to-a-usb-drive)
and select its UEFI boot entry. Both signed and unsigned images can also boot
with Secure Boot disabled, but then the firmware is not checking this seal.

Test both sides: the signed image should boot, and the unsigned one
should be refused. Use test recovery words for these checks. Secure Boot must
actually be enforcing signatures, with no legacy-boot bypass. Other trusted
keys, such as factory-installed ones, may still allow other operating systems.
The protection depends on these settings remaining under your control.

## Commands and technical details

The commands below are optional reading until you are ready to set this up.
Run them from a reviewed checkout of the release you intend to use. The build
and public verification tools use Docker; manual signing uses OpenSSL and
`sbsign` from the Linux `sbsigntool` package on your trusted host.

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

This requires the new image layout; it cannot retrofit preview 8. Check that
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
trust; they have a different role. You do not need to give landabaso control of
those ownership keys to trust his release-signing certificate.

If your manufacturer's instructions request a DER certificate, convert the
**public** certificate, not the private key:

```sh
openssl x509 -in out/release-pair/thunderden-signing.pem -outform DER \
  -out out/release-pair/thunderden-signing.cer
```

Some firmware instead requires a prepared database file or a particular USB
folder. Renaming a certificate to `.bin` is not a conversion. Follow instructions
for your exact device. **Setup Mode** is not enforced Secure Boot.

A replacement certificate does not revoke an old one. If a signing key is
compromised, remove or revoke its trust on affected laptops. Thunder Den does
not enforce a minimum release version: older validly signed software may boot.

The protected executable is a directly booted EFI Linux kernel with its complete
built-in initramfs. `CONFIG_CMDLINE_OVERRIDE` fixes its command line; built-in
`noinitrd` disables the EFI stub's external initramfs loaders. BIOS GRUB remains
a separate unauthenticated path. Neither signature checking nor byte-for-byte
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
