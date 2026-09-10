"""Dependency and syscall checks for the complete native signing test path."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1])
symbols = subprocess.check_output(["nm", "-C", "--defined-only", binary], text=True)
for expected in ("CExtKey::Derive", "SignPSBTInput", "FinalizePSBT", "PSBTInputSignedAndVerified"):
    assert expected in symbols, expected
for absent in ("CConnman::", "PeerManager", "CRPCTable::", "HTTPServer", "CWallet::", "leveldb::"):
    assert absent not in symbols, absent
print("PASS: Core key/PSBT operations linked; selected node/RPC/wallet/LevelDB symbols absent", flush=True)

libraries = subprocess.check_output(["readelf", "-d", binary], text=True)
needed = set(re.findall(r'\(NEEDED\).*\[(.*?)\]', libraries))
assert "libcrypto.so.3" in needed
assert needed <= {
    "libcrypto.so.3", "libstdc++.so.6", "libm.so.6", "libgcc_s.so.1",
    "libc.so.6", "ld-linux-x86-64.so.2",
}, needed
print("Dynamic dependencies: " + ", ".join(sorted(needed)), flush=True)
# Core's shared descriptor/signing implementation retains MuSig code even though
# Thunder Den rejects MuSig policies and input metadata. Do not claim it is absent.
print("MuSig implementation symbols retained:", "secp256k1_musig_" in symbols, flush=True)

with tempfile.TemporaryDirectory() as directory:
    stripped = Path(directory) / "transaction-tests"
    subprocess.run(["strip", "--strip-unneeded", "-o", stripped, binary], check=True)
    print(f"Stripped signing test executable (includes fixtures): {stripped.stat().st_size} bytes", flush=True)
    trace = Path(directory) / "syscalls.txt"
    command = ["strace", "-f", "-qq", "-o", str(trace), "-e", "trace=%file,%network"]
    observed = subprocess.run(command + [binary], capture_output=True, timeout=30)
    assert observed.returncode == 0 and not observed.stderr, observed.stderr
    calls = trace.read_text()
    sockets = re.findall(r'\bsocket\(([^\n]+)', calls)
    assert sockets and all(call.startswith("AF_NETLINK,") and "NETLINK_ROUTE" in call for call in sockets), calls
    assert not re.search(r'\b(connect|listen|accept|accept4)\(', calls), calls
    assert not re.search(r'O_(WRONLY|RDWR|CREAT|TRUNC|APPEND)|\b(creat|rename|unlink|mkdir)\(', calls), calls
    print("PASS: signing tests use local NETLINK_ROUTE only; no IP sockets or filesystem-write opens observed", flush=True)
    unavailable = subprocess.run(command + ["-e", "inject=socket:error=EAFNOSUPPORT", binary],
                                 capture_output=True, timeout=30)
    assert unavailable.returncode == 0 and not unavailable.stderr, unavailable.stderr
    assert unavailable.stdout == observed.stdout
    calls = trace.read_text()
    assert "EAFNOSUPPORT" in calls and "INJECTED" in calls
    print("PASS: complete signing tests also succeed with socket creation unavailable", flush=True)

application = Path(sys.argv[2])
app_symbols = subprocess.check_output(["nm", "-C", "--defined-only", application], text=True)
for absent in ("CConnman::", "PeerManager", "CRPCTable::", "HTTPServer", "CWallet::", "leveldb::", "__wrap_secp256k1"):
    assert absent not in app_symbols, absent
closure = subprocess.check_output(["ldd", application], text=True)
allowed = {
    "libcrypto.so.3", "libstdc++.so.6", "libm.so.6", "libgcc_s.so.1", "libc.so.6",
    "libqrencode.so.4", "libzbar.so.0", "libv4l2.so.0", "libv4lconvert.so.0", "libjpeg.so.62",
    # Debian's libcrypto also links its optional compression implementations.
    "libz.so.1", "libzstd.so.1",
}
loaded = set(re.findall(r'^\s*(\S+) =>', closure, re.M))
assert "not found" not in closure and loaded <= allowed, closure
print("Application library closure: " + ", ".join(sorted(loaded)), flush=True)
with tempfile.TemporaryDirectory() as directory:
    stripped = Path(directory) / "thunderden-signer"
    subprocess.run(["strip", "--strip-unneeded", "-o", stripped, application], check=True)
    print(f"Stripped native application: {stripped.stat().st_size} bytes (shared libraries separate)", flush=True)
print("PASS: native application has no selected node/RPC/wallet or test-wrapper symbols; no GUI-framework dependencies", flush=True)
