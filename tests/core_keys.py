"""Published Core key vectors and derivation-edge checks."""

import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys

SOURCE = Path("/opt/bitcoin")
BUILD = Path("/build")
HELPER = BUILD / "core-key-probe"


def source_hash():
    digest = hashlib.sha256()
    for path in sorted(SOURCE.rglob("*")):
        if path.is_symlink():
            content = str(path.readlink()).encode()
        elif path.is_file():
            content = path.read_bytes()
        else:
            continue
        digest.update(str(path.relative_to(SOURCE)).encode() + b"\0")
        digest.update(hashlib.sha256(content).digest())
    return digest.hexdigest()


def frame(data):
    return struct.pack(">I", len(data)) + data


def request(seed, path):
    return frame(seed) + frame(b"".join(struct.pack(">I", index) for index in path))


def run(data):
    result = subprocess.run([HELPER], input=data, capture_output=True, timeout=15)
    assert result.returncode == 0 and not result.stderr, result.stderr
    assert len(result.stdout) == 160
    return result.stdout


def base58check(data):
    alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
    checksum = hashlib.sha256(hashlib.sha256(data).digest()).digest()[:4]
    number = int.from_bytes(data + checksum, "big")
    result = ""
    while number:
        number, digit = divmod(number, 58)
        result = alphabet[digit] + result
    return "1" * (len(data) - len(data.lstrip(b"\0"))) + result


def check_vectors():
    # Published BIP32 vectors 1-4, supplied verbatim in the verified Core release.
    text = (SOURCE / "src/test/bip32_tests.cpp").read_text()
    vectors = re.findall(r'TestVector test[1-4] =\s*TestVector\("([0-9a-f]+)"\)(.*?);', text, re.S)
    assert len(vectors) == 4
    count = 0
    for seed_hex, body in vectors:
        steps = re.findall(r'\("([^"]+)",\s*"([^"]+)",\s*(0x[0-9a-fA-F]+|[0-9]+)\)', body)
        assert steps
        path = []
        for public, private, child in steps:
            result = run(request(bytes.fromhex(seed_hex), path))
            assert base58check(result[4:82]) == public
            assert base58check(result[82:]) == private
            count += 1
            path.append(int(child, 0))
    assert count == 17
    print(f"PASS: {count} public/private derivations from BIP32 vectors 1-4", flush=True)


def check_key_edges():
    seed = bytes(range(16))
    root = run(request(seed, []))
    assert root[:4].hex() == "3442193e"
    deep = run(request(seed, [0] * 255))
    assert deep[8] == 255 and deep[86] == 255
    subprocess.run([BUILD / "core-key-edges"], check=True)
    print("PASS: fingerprint, depth bounds and invalid-child checks", flush=True)


if __name__ == "__main__":
    if sys.argv[1:] == ["--source-hash"]:
        print(source_hash())
    else:
        assert not sys.argv[1:]
        assert source_hash() == Path("/opt/bitcoin-source.sha256").read_text().strip()
        print("PASS: verified Core source tree unchanged by configuration/build", flush=True)
        check_vectors()
        check_key_edges()
