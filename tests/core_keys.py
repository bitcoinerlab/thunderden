"""Core key regression tests and linked-code checks."""

import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

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


def check_boundaries():
    seed = bytes(range(16))
    root = run(request(seed, []))
    assert root[:4].hex() == "3442193e"
    deep = run(request(seed, [0] * 255))
    assert deep[8] == 255 and deep[86] == 255
    for malformed in [
        b"", b"\0", request(bytes(15), []), request(bytes(65), []),
        struct.pack(">I", 0xffffffff), frame(seed) + frame(b"\0"),
        frame(seed) + struct.pack(">I", 4) + b"\0",
        request(seed, [0] * 256), request(seed, []) + b"unexpected",
    ]:
        result = subprocess.run([HELPER], input=malformed, capture_output=True, timeout=15)
        assert result.returncode != 0 and not result.stdout
        assert result.stderr == b"Invalid input or key derivation failed\n"

    # Keep the helper waiting for its second field to inspect its public process metadata.
    marker = b"PUBLIC-TEST-SEED-INPUT-MARKER"
    process = subprocess.Popen([HELPER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(frame(marker))
        process.stdin.flush()
        for name in ("cmdline", "environ"):
            assert marker not in Path(f"/proc/{process.pid}/{name}").read_bytes()
        output, error = process.communicate(frame(b""), timeout=15)
        assert process.returncode == 0 and len(output) == 160 and not error
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
    subprocess.run([BUILD / "core-key-edges"], check=True)
    print("PASS: input framing, depth bounds, fingerprint and argv/environment checks", flush=True)


def inspect_linkage():
    symbols = subprocess.check_output(["nm", "-C", "--defined-only", HELPER], text=True)
    for expected in ("CExtKey::SetSeed", "CExtKey::Derive", "secp256k1_ec_pubkey_create"):
        assert expected in symbols, expected
    for absent in ("CConnman::", "PeerManager", "CRPCTable::", "HTTPServer", "CWallet::", "leveldb::", "ParseScript(", "SignPSBTInput", "MuSig2", "secp256k1_musig_", "CKey::Sign(", "UniValue::", "__wrap_secp256k1"):
        assert absent not in symbols, absent
    for archive in ("libbitcoin_node.a", "libbitcoin_wallet.a", "libleveldb.a"):
        assert not list(BUILD.rglob(archive)), archive + " unexpectedly built"

    libraries = subprocess.check_output(["readelf", "-d", HELPER], text=True)
    needed = re.findall(r'\(NEEDED\).*\[(.*?)\]', libraries)
    assert set(needed) <= {"libstdc++.so.6", "libm.so.6", "libgcc_s.so.1", "libc.so.6", "ld-linux-x86-64.so.2"}, needed
    print("Dynamic dependencies: " + ", ".join(needed), flush=True)
    subprocess.run(["size", HELPER], check=True)
    print(f"Unstripped helper: {HELPER.stat().st_size} bytes", flush=True)
    with tempfile.TemporaryDirectory() as directory:
        stripped = Path(directory) / "core-key-probe"
        subprocess.run(["strip", "--strip-unneeded", "-o", stripped, HELPER], check=True)
        print(f"Stripped helper: {stripped.stat().st_size} bytes", flush=True)
        example = request(bytes(range(16)), [0x80000000, 1])
        expected = run(example)
        trace = Path(directory) / "syscalls.txt"
        trace_command = [
            "strace", "-f", "-qq", "-o", trace, "-e", "trace=%file,%network",
        ]
        observed = subprocess.run(trace_command + [HELPER], input=example, capture_output=True)
        assert observed.returncode == 0, observed.stderr
        assert observed.stdout == expected
        calls = trace.read_text()
        # Core RNG's getifaddrs() calls glibc's local NETLINK_ROUTE interface.
        # This is kernel metadata collection, not an IP connection or peer loop.
        sockets = re.findall(r'\bsocket\(([^\n]+)', calls)
        assert sockets and all(call.startswith("AF_NETLINK,") and "NETLINK_ROUTE" in call for call in sockets), calls
        assert not re.search(r'\b(connect|listen|accept|accept4)\(', calls), calls
        assert not re.search(r'O_(WRONLY|RDWR|CREAT|TRUNC|APPEND)|\b(creat|rename|unlink|mkdir)\(', calls), calls
        print("OBSERVED: Core RNG reads system metadata and queries local NETLINK_ROUTE; no IP sockets or filesystem-write opens", flush=True)
        failed_socket = subprocess.run(trace_command + [
            "-e", "inject=socket:error=EAFNOSUPPORT", HELPER,
        ], input=example, capture_output=True)
        assert failed_socket.returncode == 0 and failed_socket.stdout == expected, failed_socket.stderr
        assert "EAFNOSUPPORT" in trace.read_text() and "INJECTED" in trace.read_text()
        print("PASS: derivation also succeeds with socket creation unavailable", flush=True)
    print("PASS: selected node/RPC/wallet/MuSig/transaction-signing symbols absent; node/wallet/LevelDB archives not built", flush=True)


if __name__ == "__main__":
    if sys.argv[1:] == ["--source-hash"]:
        print(source_hash())
    else:
        assert not sys.argv[1:]
        assert source_hash() == Path("/opt/bitcoin-source.sha256").read_text().strip()
        print("PASS: verified Core source tree unchanged by configuration/build", flush=True)
        check_vectors()
        check_boundaries()
        inspect_linkage()
