"""Check resolved kernel restrictions and inventory the installed image contents."""
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

output = Path(sys.argv[1])
target = output / "target"
config = (output / "build" / ("linux-" + os.environ["LINUX_VERSION"]) / ".config").read_text()
for symbol in (
    "NET", "BLOCK", "MODULES", "SWAP", "COREDUMP", "PSTORE", "CRASH_DUMP",
    "DEVMEM", "DEVPORT", "X86_MSR", "X86_CPUID", "X86_IOPL_IOPERM",
    "USB_STORAGE", "UAS", "SCSI", "ATA", "NVME_CORE", "MMC", "VIRTIO_BLK",
    "SUSPEND", "HIBERNATION", "KEXEC", "KEXEC_FILE", "BPF_SYSCALL", "IO_URING", "EFIVAR_FS", "USER_NS",
):
    assert not re.search(rf'^CONFIG_{symbol}=[ym]$', config, re.M), symbol + " unexpectedly enabled"
for symbol in ("X86_64", "USB_VIDEO_CLASS", "USB_HID", "VT", "FRAMEBUFFER_CONSOLE", "DEVTMPFS", "TMPFS", "SECURITY_LANDLOCK"):
    assert f"CONFIG_{symbol}=y" in config, symbol + " missing"
assert 'CONFIG_LSM="landlock"' in config

forbidden = {
    "bitcoind", "bitcoin-cli", "bitcoin-wallet", "bitcoin-node", "python", "python3",
    "gcc", "g++", "cmake", "strace", "thunderden-tests", "transaction-tests", "transport-tests",
    "application-tests", "terminal-probe", "qr-image-probe", "core-key-probe", "core-key-edges",
    "isolation-tests", "camera-tests",
}
for path in sorted(target.rglob("*")):
    relative = str(path.relative_to(target))
    assert path.name not in forbidden, relative + " is development-only"
    if path.is_file() and not path.is_symlink():
        assert not path.name.startswith(("libasan.so", "libubsan.so", "libtsan.so", "libpython", "libQt", "libX11", "libgtk")), relative
binary = target / "usr/bin/thunderden-signer"
assert binary.is_file() and os.access(binary, os.X_OK)
assert (target / "usr/bin/thunderden-scanner").is_file()
for program in ("thunderden-signer", "thunderden-scanner"):
    header = subprocess.check_output(["readelf", "-h", target / "usr/bin" / program], text=True)
    assert re.search(r"Machine:\s+Advanced Micro Devices X86-64", header), program + " must target x86-64"
assert not (target / "etc/init.d").exists()
dynamic = subprocess.check_output(["readelf", "-d", binary], text=True)
assert "/cache/" not in dynamic and "/opt/" not in dynamic, "Build-path RPATH retained"

# Inventory the packed initramfs, including /init, device nodes, ownership, modes,
# and hardlinks added by Buildroot's fakeroot step rather than just staging files.
archive = (output / "images/rootfs.cpio").read_bytes()
offset = 0
entries = []
contents = {}
while True:
    header = archive[offset:offset + 110]
    assert len(header) == 110 and header[:6] == b"070701", "Expected newc initramfs"
    values = [int(header[6 + i * 8:14 + i * 8], 16) for i in range(13)]
    inode, mode, uid, gid, links, mtime, size, major, minor, rmajor, rminor, namesize, _ = values
    offset += 110
    name = archive[offset:offset + namesize]
    assert namesize > 0 and name.endswith(b"\0")
    name = name[:-1].decode()
    offset = (offset + namesize + 3) & ~3
    payload = archive[offset:offset + size]
    assert len(payload) == size
    offset = (offset + size + 3) & ~3
    if name == "TRAILER!!!":
        assert not any(archive[offset:]), "Unexpected trailing initramfs data"
        break
    name = name[2:] if name.startswith("./") else name
    assert not name.startswith("/") and ".." not in Path(name).parts
    assert Path(name).name not in forbidden
    identity = (major, minor, inode)
    if size or identity not in contents:
        contents[identity] = payload
    entries.append((name, mode, uid, gid, links, mtime, rmajor, rminor, identity))

inventory = {}
for name, mode, uid, gid, links, mtime, major, minor, identity in entries:
    assert not mode & 0o6000, name + " has setuid/setgid permissions"
    if name in ("bin", "sbin", "lib", "lib64", "usr", "usr/bin", "usr/sbin", "usr/lib", "usr/lib64") or name.startswith(("bin/", "sbin/", "lib/", "usr/bin/", "usr/sbin/", "usr/lib/")):
        assert uid == 0 and (stat.S_ISLNK(mode) or not mode & 0o022), name + " is mutable by the application"
    item = {"mode": oct(mode), "uid": uid, "gid": gid, "links": links, "mtime": mtime}
    payload = contents[identity]
    if stat.S_ISREG(mode):
        item.update(sha256=hashlib.sha256(payload).hexdigest(), bytes=len(payload))
    elif stat.S_ISLNK(mode):
        item["symlink"] = payload.decode()
    elif stat.S_ISCHR(mode) or stat.S_ISBLK(mode):
        item["device"] = [major, minor]
    inventory[name] = item
assert "init" in inventory and "dev/console" in inventory
for required in ("bin/sh", "bin/mount", "bin/mkdir", "bin/sleep", "sbin/init", "sbin/poweroff", "usr/bin/thunderden-session"):
    assert required in inventory, required + " missing from runtime"
assert inventory["usr/bin/thunderden-signer"]["sha256"] == hashlib.sha256(binary.read_bytes()).hexdigest()
applets = [name for name, item in inventory.items() if item.get("symlink", "").endswith("busybox")]
allowed = {"ash", "sh", "init", "mount", "mkdir", "sleep", "poweroff", "test", "[", "false"}
assert all(Path(name).name in allowed for name in applets), applets
print(f"PASS: {len(applets)} BusyBox links, no setuid/setgid files, and root-owned non-writable application paths")
(output / "images/installed-files.json").write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n")
print("PASS: kernel networking/storage/crash paths disabled; required input/display drivers built in")
print(f"PASS: installed-file checks; signer {binary.stat().st_size} bytes; inventory written to installed-files.json")
