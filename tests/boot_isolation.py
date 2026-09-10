"""Developer-only containment checks on the built guest kernel and runtime libs.

Uses an extra test initramfs; test binaries never enter the distributed image.
Requires host QEMU/cpio and the test binary built with the image toolchain.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("kernel", type=Path)
parser.add_argument("test_binary", type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="thunderden-isolation-") as directory:
    root = Path(directory)
    (root / "usr/bin").mkdir(parents=True)
    (root / "etc").mkdir()
    executable = root / "usr/bin/isolation-tests"
    shutil.copyfile(args.test_binary, executable)
    executable.chmod(0o755)
    (root / "etc/inittab").write_text("""::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/bin/mount -t devtmpfs devtmpfs /dev
::sysinit:/bin/mount -t tmpfs -o mode=1777 tmpfs /tmp
ttyS0::once:/usr/bin/isolation-guest
""")
    launcher = root / "usr/bin/isolation-guest"
    launcher.write_text("""#!/bin/sh
/usr/bin/isolation-tests --guest
printf '\\nISOLATION_RESULT=%s\\n' "$?"
exec /sbin/poweroff -f
""")
    launcher.chmod(0o755)
    overlay = root / "test.cpio"
    with overlay.open("wb") as output:
        subprocess.run(["cpio", "-o", "-H", "newc"], cwd=root,
                       input=b"usr\nusr/bin\nusr/bin/isolation-tests\nusr/bin/isolation-guest\netc\netc/inittab\n",
                       stdout=output, check=True)
    result = subprocess.run(["qemu-system-x86_64", "-m", "1024", "-smp", "2", "-cpu", "qemu64",
                             "-kernel", str(args.kernel.resolve()), "-initrd", str(overlay),
                             "-append", "console=ttyS0 loglevel=3", "-display", "none", "-serial", "stdio",
                             "-nic", "none", "-no-reboot"], capture_output=True, timeout=90)
    text = result.stdout.decode(errors="replace")
    assert result.returncode == 0 and "ISOLATION_RESULT=0" in text, text + result.stderr.decode(errors="replace")
    print("PASS: containment, hostile IPC, and real JPEG/QR/UR decoding on the image kernel/runtime")
