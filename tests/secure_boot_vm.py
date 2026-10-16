"""Firmware enforcement tests. All generated keys are disposable and temporary.

Run in the boot-tests container. Never enroll its certificates on a real laptop.
Screenshots, firmware-variable stores and serial logs are public test evidence.
"""
import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
from secure_boot import START, certificate_fingerprint

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("unsigned", type=Path)
parser.add_argument("output", type=Path, help="New directory for public test evidence")
parser.add_argument("--unprotected-kernel", type=Path, help="Optional pre-fix EFI kernel for an injection positive control")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
tool = Path(__file__).resolve().parents[1] / "build/secure_boot.py"


def run(*arguments):
    result = subprocess.run([str(x) for x in arguments], capture_output=True)
    assert result.returncode == 0, (arguments, result.stdout, result.stderr)
    return result.stdout


def copy_into(image, source, target):
    run("mcopy", "-o", "-i", f"{image}@@{START}", source, "::" + target)


def boot(image, name, firmware, variables, rtc=None):
    output = args.output / name
    output.mkdir()
    shutil.copyfile(variables, output / "vars.fd")
    # Unix socket paths are short even when evidence is written deep below /out.
    with tempfile.TemporaryDirectory() as directory:
        qmp = Path(directory) / "qmp.sock"
        command = ["qemu-system-x86_64", "-machine", "q35,smm=on", "-m", "1024", "-smp", "2",
                   "-cpu", "qemu64", "-global", "driver=cfi.pflash01,property=secure,value=on",
                   "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={firmware}",
                   "-drive", f"if=pflash,format=raw,unit=1,file={output / 'vars.fd'}",
                   "-drive", f"file={image},format=raw,if=ide,snapshot=on",
                   "-device", "virtio-rng-pci", "-display", "none", "-nic", "none", "-no-reboot",
                   "-serial", f"file:{output / 'serial.log'}", "-qmp", f"unix:{qmp},server=on,wait=off"]
        if rtc:
            command += ["-rtc", f"base={rtc}"]
        with (output / "qemu.log").open("wb") as log:
            process = subprocess.Popen(command, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 20
                while not qmp.exists():
                    assert process.poll() is None and time.monotonic() < deadline, "QEMU startup failed"
                    time.sleep(0.1)
                connection = socket.socket(socket.AF_UNIX)
                connection.settimeout(10)
                while True:
                    try:
                        connection.connect(str(qmp))
                        break
                    except ConnectionRefusedError:
                        assert process.poll() is None and time.monotonic() < deadline, "QMP unavailable"
                        time.sleep(0.1)
                stream = connection.makefile("rwb")
                json.loads(stream.readline())

                def request(name, arguments=None):
                    stream.write(json.dumps({"execute": name, "arguments": arguments or {}}).encode() + b"\n")
                    stream.flush()
                    while True:
                        response = json.loads(stream.readline())
                        assert "error" not in response, response
                        if "return" in response:
                            return response["return"]

                request("qmp_capabilities")
                time.sleep(30)
                request("screendump", {"filename": str(output / "screen.ppm")})
                request("quit")
                process.wait(timeout=10)
                stream.close()
                connection.close()
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
    return (output / "screen.ppm").read_bytes(), (output / "serial.log").read_bytes()


def evil_initramfs():
    archive = bytearray()

    def entry(name, mode, data=b"", rmajor=0, rminor=0):
        fields = (1, mode, 0, 0, 1, 0, len(data), 0, 0, rmajor, rminor, len(name) + 1, 0)
        archive.extend(b"070701" + "".join(f"{value:08x}" for value in fields).encode() + name.encode() + b"\0")
        archive.extend(bytes(-len(archive) % 4))
        archive.extend(data)
        archive.extend(bytes(-len(archive) % 4))

    entry("dev/ttyS0", stat.S_IFCHR | 0o600, rmajor=4, rminor=64)
    entry("init", stat.S_IFREG | 0o755, b"#!/bin/sh\necho TD_EVIL_INITRD_EXECUTED > /dev/ttyS0\nexec /bin/sh\n")
    entry("TRAILER!!!", 0)
    return archive


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for name in ("test", "other"):
        run("openssl", "req", "-new", "-x509", "-newkey", "rsa:3072", "-nodes", "-days", "36525",
            "-subj", f"/CN=ThunderDen-DISPOSABLE-{name}-TEST-ONLY/", "-keyout", root / f"{name}.key",
            "-addext", "basicConstraints=critical,CA:FALSE", "-addext", "keyUsage=critical,digitalSignature",
            "-addext", "extendedKeyUsage=codeSigning",
            "-out", root / f"{name}.pem")
    certificate = root / "test.pem"
    run("openssl", "x509", "-in", certificate, "-noout", "-checkend", str(36524 * 86400))
    assert b"GENERALIZEDTIME" in run("openssl", "asn1parse", "-in", certificate)
    (args.output / "test-certificate-dates.txt").write_bytes(run("openssl", "x509", "-in", certificate, "-noout", "-dates"))
    efi = root / "unsigned.efi"
    run(sys.executable, "-B", tool, "prepare", args.unsigned, efi)
    signature = root / "boot.pk7"
    run("sbsign", "--detached", "--key", root / "test.key", "--cert", certificate, "--output", signature, efi)
    release = root / "release"
    run(sys.executable, "-B", tool, "package", args.unsigned, signature, certificate, release,
        "--fingerprint", certificate_fingerprint(certificate))
    signed_image = release / "thunderden-signed.img"
    signed_efi = root / "signed.efi"
    run("mcopy", "-i", f"{signed_image}@@{START}", "::/EFI/BOOT/BOOTX64.EFI", signed_efi)
    # Keep the test pair and public certificate, never the temporary private keys.
    shutil.copytree(release, args.output / "DISPOSABLE-TEST-RELEASE")
    firmware = Path("/usr/share/OVMF/OVMF_CODE_4M.secboot.fd")
    plain_variables = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
    secure_variables = root / "secure-vars.fd"
    guid = "6b386380-6d72-4a19-b19d-9aa0db84eff8"
    run("virt-fw-vars", "--input", plain_variables, "--output", secure_variables,
        "--set-pk", guid, certificate, "--add-kek", guid, certificate, "--add-db", guid, certificate, "--sb")
    run("gcc", "-I/usr/include/efi", "-I/usr/include/efi/x86_64", "-ffreestanding", "-fpic", "-fshort-wchar", "-mno-red-zone",
        "-fno-stack-protector", "-DEFI_FUNCTION_WRAPPER", "-Wall", "-Wextra", "-Werror", "-c",
        Path(__file__).with_name("efi_boot_probe.c"), "-o", root / "probe.o")
    run("ld", "-nostdlib", "-znocombreloc", "-T", "/usr/lib/elf_x86_64_efi.lds", "-shared", "-Bsymbolic",
        "/usr/lib/crt0-efi-x86_64.o", root / "probe.o", "-L/usr/lib", "-lefi", "-lgnuefi", "-o", root / "probe.so")
    run("objcopy", "-j", ".text", "-j", ".rodata", "-j", ".sdata", "-j", ".data", "-j", ".dynamic", "-j", ".dynsym",
        "-j", ".rel", "-j", ".rela", "-j", ".reloc", "--target=efi-app-x86_64", root / "probe.so", root / "probe.efi")
    run("sbsign", "--key", root / "test.key", "--cert", certificate, "--output", root / "probe-signed.efi", root / "probe.efi")
    baseline, _ = boot(args.unsigned, "unsigned-secure-boot-off", firmware, plain_variables)
    assert baseline.startswith(b"P6"), "Missing baseline screenshot"
    for image, name, variables in ((signed_image, "signed-secure-boot-off", plain_variables),
                                    (signed_image, "signed-secure-boot-on", secure_variables)):
        screenshot, _ = boot(image, name, firmware, variables)
        assert screenshot == baseline, name + " did not reach the same Thunder Den screen"
        print("PASS:", name, flush=True)
    future = (datetime.now(timezone.utc) + timedelta(days=30 * 365.25)).strftime("%Y-%m-%dT%H:%M:%S")
    (args.output / "future-rtc.txt").write_text(future + "\n")
    screenshot, _ = boot(signed_image, "signed-secure-boot-in-30-years", firmware, secure_variables, rtc=future)
    assert screenshot == baseline, "Long-lived certificate did not boot with the VM clock 30 years ahead"
    print("PASS: 100-year certificate; Secure Boot with VM clock 30 years ahead", flush=True)
    rejected, serial = boot(args.unsigned, "unsigned-secure-boot-on", firmware, secure_variables)
    assert rejected != baseline and b"Access Denied" in serial, "Unsigned image was not rejected by firmware"
    other_efi = root / "other.efi"
    run("sbsign", "--key", root / "other.key", "--cert", root / "other.pem", "--output", other_efi, efi)
    damaged = root / "damaged.efi"
    data = bytearray(signed_efi.read_bytes())
    data[len(data) // 2] ^= 1  # The compressed kernel/runtime payload, not its signature table.
    damaged.write_bytes(data)
    for kernel, name in ((efi, "unsigned-rejected"), (other_efi, "wrong-key-rejected"), (damaged, "modified-runtime-rejected")):
        image = root / f"{name}.img"
        shutil.copyfile(signed_image, image)
        copy_into(image, root / "probe-signed.efi", "/EFI/BOOT/BOOTX64.EFI")
        copy_into(image, kernel, "/kernel.efi")
        screenshot, serial = boot(image, name, firmware, secure_variables)
        assert b"TD_TEST_SECURE_BOOT_ON" in serial, serial[:2000]
        assert b"TD_TEST_SECURITY_VIOLATION" in serial or b"TD_TEST_ACCESS_DENIED" in serial, serial[:2000]
        assert b"TD_TEST_START_KERNEL" not in serial and screenshot != baseline
        print("PASS:", name, flush=True)
    for provider in (False, True):
        name = "hostile-loadfile2-and-options" if provider else "hostile-options"
        image = root / f"{name}.img"
        shutil.copyfile(signed_image, image)
        copy_into(image, root / "probe-signed.efi", "/EFI/BOOT/BOOTX64.EFI")
        copy_into(image, signed_efi, "/kernel.efi")
        if provider:
            evil = root / "evil.cpio"
            evil.write_bytes(evil_initramfs())
            copy_into(image, evil, "/evil.cpio")
        screenshot, serial = boot(image, name, firmware, secure_variables)
        assert b"TD_TEST_SECURE_BOOT_ON" in serial and b"TD_TEST_START_KERNEL_WITH_HOSTILE_OPTIONS" in serial, serial[:2000]
        assert b"TD_TEST_LOADFILE2_CALLED" not in serial and b"TD_EVIL_INITRD_EXECUTED" not in serial, serial[:2000]
        assert not provider or b"TD_TEST_LOADFILE2_INSTALLED" in serial, serial[:2000]
        assert screenshot == baseline, name + " changed authenticated startup"
        print("PASS:", name, flush=True)
    image = root / "hostile-config.img"
    shutil.copyfile(signed_image, image)
    config = root / "grub.cfg"
    config.write_text('menuentry "EVIL" { linux /bzImage rdinit=/bin/sh; initrd /evil.cpio; }\n')
    copy_into(image, config, "/boot/grub/grub.cfg")
    copy_into(image, config, "/EFI/BOOT/grub.cfg")
    screenshot, _ = boot(image, "unsigned-grub-config-ignored", firmware, secure_variables)
    assert screenshot == baseline, "Unsigned GRUB configuration affected direct authenticated EFI boot"
    if args.unprotected_kernel:
        old = root / "old-signed.efi"
        run("sbsign", "--key", root / "test.key", "--cert", certificate, "--output", old, args.unprotected_kernel)
        image = root / "injection-positive-control.img"
        shutil.copyfile(signed_image, image)
        copy_into(image, root / "probe-signed.efi", "/EFI/BOOT/BOOTX64.EFI")
        copy_into(image, old, "/kernel.efi")
        copy_into(image, evil, "/evil.cpio")
        screenshot, serial = boot(image, "injection-positive-control", firmware, secure_variables)
        assert b"TD_TEST_LOADFILE2_CALLED" in serial, serial[:2000]
        assert screenshot != baseline, "Pre-fix kernel unexpectedly ignored the hostile boot inputs"
        print("PASS: pre-fix kernel accepts the injection that the protected kernel ignores", flush=True)
print("PASS: firmware verification, trusted-key enforcement, runtime tampering and external boot-input isolation")
