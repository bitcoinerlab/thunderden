"""Public-only Secure Boot packaging. Private keys never enter this tool."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from image import assemble


SIZE = 64 * 1024 * 1024
START = 2048 * 512


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def certificate_fingerprint(certificate):
    der = subprocess.check_output(["openssl", "x509", "-in", certificate, "-outform", "DER"])
    return digest(der)


def check_certificate(certificate, expected):
    expected = expected.lower().replace(":", "")
    require(len(expected) == 64 and all(c in "0123456789abcdef" for c in expected),
            "Supply the independently trusted certificate's SHA-256 fingerprint")
    require(certificate_fingerprint(certificate) == expected, "Signing certificate fingerprint does not match")


def extract(unsigned, root):
    """Accept only our canonical unsigned layout; retain no arbitrary extra files."""
    require(unsigned.stat().st_size == SIZE, "Expected a 64 MiB Thunder Den image")
    raw = unsigned.read_bytes()
    require(raw[510:512] == b"\x55\xaa", "Invalid disk image")
    snapshot = root / "input.img"
    snapshot.write_bytes(raw)
    (root / "boot.img").write_bytes(raw[:512])
    (root / "grub.img").write_bytes(raw[512:START])
    for source, target in (("/EFI/BOOT/BOOTX64.EFI", "thunderden.efi"),
                           ("/bzImage", "bzImage"), ("/boot/grub/grub.cfg", "grub.cfg"),
                           ("/boot/boot-policy.json", "boot-policy.json")):
        subprocess.run(["mcopy", "-i", f"{snapshot}@@{START}", "::" + source, root / target], check=True)
    policy = json.loads((root / "boot-policy.json").read_text())
    require(set(policy) == {"format", "epoch"} and policy["format"] == 1
            and type(policy["epoch"]) is int, "Unsupported boot-image format")
    require(315532800 <= policy["epoch"] <= 4354819199, "Invalid image timestamp")
    efi = (root / "thunderden.efi").read_bytes()
    require(efi.startswith(b"MZ") and efi == (root / "bzImage").read_bytes(),
            "Unsigned EFI payload must be the same self-contained kernel as the BIOS payload")
    canonical = root / "canonical.img"
    assemble(root, root / "grub.cfg", canonical, policy["epoch"])
    require(canonical.read_bytes() == raw,
            "Image is not the canonical unsigned layout (or packaging tools differ)")
    return policy["epoch"]


def attach(unsigned, signature, certificate, fingerprint, root):
    check_certificate(certificate, fingerprint)
    require(0 < signature.stat().st_size <= 65536, "Invalid detached signature size")
    epoch = extract(unsigned, root)
    original = root / "thunderden.efi"
    subprocess.run(["sbverify", "--cert", certificate, "--detached", signature, original], check=True)
    signed = root / "signed.efi"
    shutil.copyfile(original, signed)
    subprocess.run(["sbattach", "--attach", signature, signed], check=True)
    subprocess.run(["sbverify", "--cert", certificate, signed], check=True)
    result = root / "thunderden-signed.img"
    assemble(root, root / "grub.cfg", result, epoch, signed)
    return result


def prepare(args):
    require(not args.efi.exists(), "Output already exists; choose a new EFI filename")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        extract(args.unsigned, root)
        data = (root / "thunderden.efi").read_bytes()
        with args.efi.open("xb") as output:
            output.write(data)
    print(f"EFI SHA-256: {digest(data)}")
    print("Sign this exact file manually with sbsign --detached; do not share your private key or password.")


def package(args):
    require(not args.output.exists(), "Release directory already exists; choose a new directory")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        signed = attach(args.unsigned, args.signature, args.certificate, args.fingerprint, root)
        certificate = subprocess.check_output(["openssl", "x509", "-in", args.certificate, "-outform", "PEM"])
        metadata = {
            "format": 1,
            "certificate_sha256": certificate_fingerprint(args.certificate),
            "unsigned_efi_sha256": digest((root / "thunderden.efi").read_bytes()),
            "signed_efi_sha256": digest((root / "signed.efi").read_bytes()),
            "signature_sha256": digest(args.signature.read_bytes()),
            "unsigned_image_sha256": digest((root / "input.img").read_bytes()),
            "signed_image_sha256": digest(signed.read_bytes()),
        }
        args.output.mkdir()
        for source, name in ((root / "input.img", "thunderden.img"),
                             (signed, "thunderden-signed.img"), (args.signature, "thunderden-boot.pk7")):
            shutil.copyfile(source, args.output / name)
        (args.output / "thunderden-signing.pem").write_bytes(certificate)
        (args.output / "secure-boot.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
        for name in ("thunderden.img", "thunderden-signed.img"):
            value = digest((args.output / name).read_bytes())
            (args.output / (name + ".sha256")).write_text(f"{value}  {name}\n")
    print("Created unsigned/signed release pair, detached EFI signature and public certificate.")


def verify(args):
    with tempfile.TemporaryDirectory() as directory:
        expected = attach(args.unsigned, args.signature, args.certificate, args.fingerprint, Path(directory))
        require(args.signed.stat().st_size == SIZE and args.signed.read_bytes() == expected.read_bytes(),
                "Signed image differs from the exact reconstruction; do not use it")
        print(f"PASS: exact signed-image reconstruction: {digest(expected.read_bytes())}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("prepare", help="Extract the exact unsigned EFI artifact for manual signing")
    command.add_argument("unsigned", type=Path)
    command.add_argument("efi", type=Path)
    command.set_defaults(run=prepare)
    for name, function in (("package", package), ("verify", verify)):
        command = commands.add_parser(name, help="Package a release pair" if name == "package" else "Verify exact reconstruction")
        command.add_argument("unsigned", type=Path)
        if name == "verify":
            command.add_argument("signed", type=Path)
        command.add_argument("signature", type=Path)
        command.add_argument("certificate", type=Path, help="Public PEM certificate, never a private key")
        if name == "package":
            command.add_argument("output", type=Path, help="New release directory")
        command.add_argument("--fingerprint", required=True, help="Independently trusted certificate SHA-256")
        command.set_defaults(run=function)
    args = parser.parse_args()
    try:
        args.run(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Secure Boot packaging failed: {error}\n")


if __name__ == "__main__":
    main()
