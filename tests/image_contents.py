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
    "SUSPEND", "HIBERNATION", "KEXEC", "KEXEC_FILE", "BPF_SYSCALL", "IO_URING", "EFIVAR_FS",
):
    assert not re.search(rf'^CONFIG_{symbol}=[ym]$', config, re.M), symbol + " unexpectedly enabled"
for symbol in ("USB_VIDEO_CLASS", "USB_HID", "VT", "FRAMEBUFFER_CONSOLE", "DEVTMPFS", "TMPFS"):
    assert f"CONFIG_{symbol}=y" in config, symbol + " missing"

forbidden = {
    "bitcoind", "bitcoin-cli", "bitcoin-wallet", "bitcoin-node", "python", "python3",
    "gcc", "g++", "cmake", "strace", "thunderden-tests", "transaction-tests", "transport-tests",
    "application-tests", "terminal-probe", "qr-image-probe", "core-key-probe", "core-key-edges",
}
for path in sorted(target.rglob("*")):
    relative = str(path.relative_to(target))
    assert path.name not in forbidden, relative + " is development-only"
    if path.is_file() and not path.is_symlink():
        assert not path.name.startswith(("libasan.so", "libubsan.so", "libtsan.so", "libpython", "libQt", "libX11", "libgtk")), relative
binary = target / "usr/bin/thunderden-signer"
assert binary.is_file() and os.access(binary, os.X_OK)
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
assert inventory["usr/bin/thunderden-signer"]["sha256"] == hashlib.sha256(binary.read_bytes()).hexdigest()
(output / "images/installed-files.json").write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n")
print("PASS: kernel networking/storage/crash paths disabled; required input/display drivers built in")
print(f"PASS: installed-file checks; signer {binary.stat().st_size} bytes; inventory written to installed-files.json")
