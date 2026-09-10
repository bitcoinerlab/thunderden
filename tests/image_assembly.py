"""Reassemble the same boot payloads under different timestamps and time zones."""
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
from image import assemble

images, config = map(Path, sys.argv[1:3])
epoch = int(os.environ["SOURCE_DATE_EPOCH"])
os.environ["PATH"] = str(images.parent / "host/sbin") + ":" + str(images.parent / "host/bin") + ":" + os.environ["PATH"]
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    copied = root / "images"
    (copied / "efi-part/EFI/BOOT").mkdir(parents=True)
    for name in ("boot.img", "grub.img", "bzImage", "efi-part/EFI/BOOT/bootx64.efi"):
        shutil.copyfile(images / name, copied / name)
    local_config = root / "grub.cfg"
    shutil.copyfile(config, local_config)
    results = []
    for index, zone in enumerate(("UTC", "Pacific/Auckland")):
        for path in [*copied.rglob("*"), local_config]:
            os.utime(path, (epoch + index * 86400 + 123, epoch + index * 86400 + 123))
        os.environ["TZ"] = zone
        result = root / f"image-{index}.img"
        assemble(copied, local_config, result, epoch)
        results.append(hashlib.sha256(result.read_bytes()).digest())
    assert results[0] == results[1], "FAT/disk metadata is nondeterministic"
print("PASS: disk image identical despite different source timestamps and time zones")
