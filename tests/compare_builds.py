"""Compare image artifacts from two independently completed clean builds."""
import argparse
import hashlib
import json
from pathlib import Path


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("first", type=Path, help="First build's images directory")
parser.add_argument("second", type=Path, help="Second build's images directory")
args = parser.parse_args()
if args.first.resolve() == args.second.resolve():
    parser.error("Supply two different build directories")

different = False
for name in (
    "boot.img", "grub.img", "efi-part/EFI/BOOT/bootx64.efi", "rootfs.cpio",
    "bzImage", "thunderden.img", "thunderden.img.sha256", "installed-files.json",
):
    first = (args.first / name).read_bytes()
    second = (args.second / name).read_bytes()
    digest = hashlib.sha256(first).hexdigest()
    if first == second:
        print(f"MATCH {name}: {len(first)} bytes, SHA256 {digest}")
        continue
    different = True
    print(f"DIFFER {name}")
    print(f"  first:  {len(first)} bytes, SHA256 {digest}")
    print(f"  second: {len(second)} bytes, SHA256 {hashlib.sha256(second).hexdigest()}")
    if name == "installed-files.json":
        left, right = json.loads(first), json.loads(second)
        for path in sorted(left.keys() | right.keys()):
            if left.get(path) != right.get(path):
                print(f"  inventory difference: {path}")
                print(f"    first:  {left.get(path)}")
                print(f"    second: {right.get(path)}")

if different:
    raise SystemExit("FAIL: clean-build artifacts differ")
print("PASS: complete disk image, boot payloads, and installed-file inventory are byte-identical")
