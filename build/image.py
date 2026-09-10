"""Rootless, deterministic 64 MiB BIOS + UEFI disk-image assembly."""
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def normalize_fat(path, epoch):
    data = bytearray(path.read_bytes())
    u16 = lambda offset: struct.unpack_from("<H", data, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    sector = u16(11)
    sectors_per_cluster = data[13]
    reserved, fats, fat_sectors = u16(14), data[16], u32(36)
    assert sector == 512 and sectors_per_cluster > 0 and fat_sectors > 0
    cluster_bytes = sector * sectors_per_cluster
    fat_offset = reserved * sector
    start = (reserved + fats * fat_sectors) * sector
    stamp = datetime.fromtimestamp(epoch, timezone.utc)
    assert 1980 <= stamp.year <= 2107
    date = ((stamp.year - 1980) << 9) | (stamp.month << 5) | stamp.day
    time = (stamp.hour << 11) | (stamp.minute << 5) | (stamp.second // 2)
    visited = set()

    def directory(cluster):
        while cluster < 0x0ffffff8:
            assert cluster >= 2 and cluster not in visited
            visited.add(cluster)
            offset = start + (cluster - 2) * cluster_bytes
            assert offset + cluster_bytes <= len(data)
            for entry in range(offset, offset + cluster_bytes, 32):
                if data[entry] == 0:
                    return
                if data[entry] == 0xe5 or data[entry + 11] == 0x0f:
                    continue  # Deleted / long-filename entries are not timestamps.
                data[entry + 13] = 0
                struct.pack_into("<HHH", data, entry + 14, time, date, date)
                struct.pack_into("<HH", data, entry + 22, time, date)
                if data[entry + 11] & 0x10 and data[entry] != ord("."):
                    directory((u16(entry + 20) << 16) | u16(entry + 26))
            cluster = u32(fat_offset + cluster * 4) & 0x0fffffff

    directory(u32(44))
    path.write_bytes(data)


def chs(lba):
    cylinder, remainder = divmod(lba, 255 * 63)
    if cylinder > 1023:
        return b"\xfe\xff\xff"
    head, sector = divmod(remainder, 63)
    return bytes((head, (sector + 1) | ((cylinder >> 2) & 0xc0), cylinder & 0xff))


def assemble(images, config, output, epoch):
    start = 2048
    sectors = 64 * 1024 * 1024 // 512
    boot = (images / "boot.img").read_bytes()
    core = (images / "grub.img").read_bytes()
    assert len(boot) == 512 and 0 < len(core) <= (start - 1) * 512
    os.environ["PATH"] = str(images.parent / "host/sbin") + ":" + str(images.parent / "host/bin") + ":" + os.environ["PATH"]
    with tempfile.TemporaryDirectory() as temporary:
        fat = Path(temporary) / "esp.fat"
        with fat.open("wb") as stream:
            stream.truncate((sectors - start) * 512)
        subprocess.run(["mkfs.fat", "--invariant", "-F", "32", "-i", "54445532", "-n", "THUNDERDEN", "-h", str(start), fat], check=True)
        subprocess.run(["mmd", "-i", fat, "::/EFI", "::/EFI/BOOT", "::/boot", "::/boot/grub"], check=True)
        for source, destination in [
            (images / "efi-part/EFI/BOOT/bootx64.efi", "::/EFI/BOOT/BOOTX64.EFI"),
            (config, "::/EFI/BOOT/grub.cfg"),
            (config, "::/boot/grub/grub.cfg"),
            (images / "bzImage", "::/bzImage"),
        ]:
            subprocess.run(["mcopy", "-i", fat, source, destination], check=True)
        normalize_fat(fat, epoch)
        mbr = bytearray(512)
        mbr[:440] = boot[:440]
        struct.pack_into("<I", mbr, 440, 0x54445532)
        mbr[446:462] = struct.pack("<B3sB3sII", 0x80, chs(start), 0xef, chs(sectors - 1), start, sectors - start)
        mbr[510:] = b"\x55\xaa"
        with output.open("wb") as disk:
            disk.truncate(sectors * 512)
            disk.write(mbr)
            disk.write(core)
            disk.seek(start * 512)
            disk.write(fat.read_bytes())
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(output.suffix + ".sha256").write_text(f"{digest}  {output.name}\n")
    print(f"{digest}  {output}")


if __name__ == "__main__":
    images, config = map(Path, sys.argv[1:3])
    output = Path(sys.argv[3]) if len(sys.argv) == 4 else images / "thunderden.img"
    assemble(images, config, output, int(os.environ["SOURCE_DATE_EPOCH"]))
