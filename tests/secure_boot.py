"""Exercise public-only release packaging with a real image and disposable keys."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
from secure_boot import certificate_fingerprint

tool = Path(__file__).resolve().parents[1] / "build/secure_boot.py"
unsigned = Path(sys.argv[1]).resolve()


def run(*arguments, success=True, env=None):
    result = subprocess.run([str(x) for x in arguments], capture_output=True, env=env)
    assert (result.returncode == 0) == success, (arguments, result.stdout, result.stderr)
    return result


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for name in ("test", "other"):
        run("openssl", "req", "-new", "-x509", "-newkey", "rsa:3072", "-nodes", "-days", "36525",
            "-subj", f"/CN=ThunderDen-DISPOSABLE-{name}-TEST-ONLY/", "-keyout", root / f"{name}.key",
            "-addext", "basicConstraints=critical,CA:FALSE", "-addext", "keyUsage=critical,digitalSignature",
            "-addext", "extendedKeyUsage=codeSigning",
            "-out", root / f"{name}.pem")
    certificate = root / "test.pem"
    # Exercise the documented century-long validity and post-2050 ASN.1 date format.
    run("openssl", "x509", "-in", certificate, "-noout", "-checkend", str(36524 * 86400))
    assert b"GENERALIZEDTIME" in run("openssl", "asn1parse", "-in", certificate).stdout
    fingerprint = certificate_fingerprint(certificate)
    efi = root / "unsigned.efi"
    run(sys.executable, "-B", tool, "prepare", unsigned, efi)
    run(sys.executable, "-B", tool, "prepare", unsigned, efi, success=False)  # Never overwrite.
    signature = root / "boot.pk7"
    run("sbsign", "--detached", "--key", root / "test.key", "--cert", certificate, "--output", signature, efi)
    # Disposable password only: confirm the manual tool supports an encrypted key prompt.
    encrypted = root / "encrypted-test.key"
    run("openssl", "pkey", "-in", root / "test.key", "-aes-256-cbc", "-passout",
        "pass:DISPOSABLE-TEST-PASSWORD", "-out", encrypted)
    prompted_signature = root / "prompted.pk7"
    prompted = subprocess.run(["sbsign", "--detached", "--key", str(encrypted), "--cert", str(certificate),
                              "--output", str(prompted_signature), str(efi)],
                             input=b"DISPOSABLE-TEST-PASSWORD\n", capture_output=True)
    assert prompted.returncode == 0 and b"pass phrase" in prompted.stderr.lower(), prompted.stderr
    run("sbverify", "--cert", certificate, "--detached", prompted_signature, efi)
    releases = []
    for index, zone in enumerate(("UTC", "Pacific/Auckland")):
        output = root / f"release-{index}"
        run(sys.executable, "-B", tool, "package", unsigned, signature, certificate, output,
            "--fingerprint", fingerprint, env={**os.environ, "TZ": zone})
        releases.append(output)
        metadata = json.loads((output / "secure-boot.json").read_text())
        assert metadata["certificate_sha256"] == fingerprint
        assert metadata["unsigned_efi_sha256"] == hashlib.sha256(efi.read_bytes()).hexdigest()
        assert (output / "thunderden.img").read_bytes() == unsigned.read_bytes()
        assert not any(p.suffix == ".key" for p in output.iterdir())
        run(sys.executable, "-B", tool, "verify", output / "thunderden.img", output / "thunderden-signed.img",
            output / "thunderden-boot.pk7", output / "thunderden-signing.pem", "--fingerprint", fingerprint)
    for path in releases[0].iterdir():
        assert path.read_bytes() == (releases[1] / path.name).read_bytes(), "Nondeterministic release packaging"
    signed = releases[0] / "thunderden-signed.img"
    run(sys.executable, "-B", tool, "package", unsigned, signature, certificate, releases[0],
        "--fingerprint", fingerprint, success=False)
    run(sys.executable, "-B", tool, "prepare", signed, root / "not-unsigned.efi", success=False)
    for cert, fp in ((root / "other.pem", fingerprint), (certificate, "0" * 64),
                     (root / "other.pem", certificate_fingerprint(root / "other.pem"))):
        run(sys.executable, "-B", tool, "package", unsigned, signature, cert, root / "rejected",
            "--fingerprint", fp, success=False)
        assert not (root / "rejected").exists()
    damaged_signature = root / "bad.pk7"
    damaged_signature.write_bytes(signature.read_bytes()[:-32] + bytes(32))
    run(sys.executable, "-B", tool, "package", unsigned, damaged_signature, certificate, root / "rejected",
        "--fingerprint", fingerprint, success=False)
    # Even a valid trusted-key signature cannot substitute different runtime bytes.
    altered_efi = root / "altered.efi"
    altered = bytearray(efi.read_bytes())
    altered[len(altered) // 2] ^= 1
    altered_efi.write_bytes(altered)
    altered_signature = root / "altered.pk7"
    run("sbsign", "--detached", "--key", root / "test.key", "--cert", certificate,
        "--output", altered_signature, altered_efi)
    run("sbverify", "--cert", certificate, "--detached", altered_signature, altered_efi)
    run(sys.executable, "-B", tool, "package", unsigned, altered_signature, certificate, root / "rejected",
        "--fingerprint", fingerprint, success=False)
    assert not (root / "rejected").exists()
    # Exact whole-image comparison catches modifications even in unsigned unused space.
    for original, name in ((unsigned, "unsigned"), (signed, "signed")):
        damaged = root / f"bad-{name}.img"
        data = bytearray(original.read_bytes())
        data[-1] ^= 1
        damaged.write_bytes(data)
        run(sys.executable, "-B", tool, "verify", damaged if name == "unsigned" else unsigned,
            damaged if name == "signed" else signed, signature, certificate,
            "--fingerprint", fingerprint, success=False)
    # A new signer can sign the same unsigned image without rebuilding any software.
    own_signature = root / "own.pk7"
    own_certificate = root / "other.pem"
    run("sbsign", "--detached", "--key", root / "other.key", "--cert", own_certificate,
        "--output", own_signature, efi)
    run(sys.executable, "-B", tool, "package", unsigned, own_signature, own_certificate, root / "own-release",
        "--fingerprint", certificate_fingerprint(own_certificate))
    assert (root / "own-release/thunderden.img").read_bytes() == unsigned.read_bytes()
print("PASS: 100-year certificates, real EFI signatures, exact release reconstruction, timestamp independence, rejection paths and user self-signing")
