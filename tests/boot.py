"""Boot the actual disk image and export a public fixture account through its UI.

Requires host QEMU and a built Docker test image. Output screenshots and QEMU logs
are retained for inspection. All recovery words used here are public fixtures.
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("image", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--firmware", type=Path, help="Combined OVMF firmware image; omit for SeaBIOS")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
qmp_path = args.output / "qmp.sock"
qmp_path.unlink(missing_ok=True)
command = ["qemu-system-x86_64", "-m", "1024", "-smp", "2", "-cpu", "qemu64",
           "-drive", f"file={args.image.resolve()},format=raw,if=ide,snapshot=on",
           "-device", "virtio-rng-pci", "-display", "none", "-nic", "none",
           "-qmp", f"unix:{qmp_path},server=on,wait=off", "-no-reboot"]
if args.firmware:
    command += ["-bios", str(args.firmware)]
with (args.output / "qemu.log").open("wb") as log:
    process = subprocess.Popen(command, stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + 15
        while not qmp_path.exists():
            assert process.poll() is None, "QEMU exited; inspect qemu.log"
            assert time.monotonic() < deadline, "QMP socket did not appear"
            time.sleep(0.1)
        connection = socket.socket(socket.AF_UNIX)
        connection.settimeout(10)
        while True:
            try:
                connection.connect(str(qmp_path))
                break
            except ConnectionRefusedError:
                assert process.poll() is None and time.monotonic() < deadline, "QEMU exited or QMP unavailable; inspect qemu.log"
                time.sleep(0.1)
        stream = connection.makefile("rwb")
        json.loads(stream.readline())

        def qmp(name, arguments=None):
            stream.write(json.dumps({"execute": name, "arguments": arguments or {}}).encode() + b"\n")
            stream.flush()
            while True:
                response = json.loads(stream.readline())
                assert "error" not in response, response
                if "return" in response:
                    return response["return"]

        def key(value):
            qmp("human-monitor-command", {"command-line": "sendkey " + value + " 40"})
            time.sleep(0.07)

        def text(value):
            for char in value:
                key("spc" if char == " " else "ret" if char == "\n" else "shift-" + char.lower() if char.isupper() else char)

        def screenshot(name):
            qmp("screendump", {"filename": str((args.output / name).resolve())})

        qmp("qmp_capabilities")
        time.sleep(25)
        screenshot("network.ppm")
        key("4")  # Regtest
        time.sleep(2)
        key("2")  # Export account
        time.sleep(1)
        key("1")  # BIP84
        time.sleep(1)
        key("ret")  # Account zero
        time.sleep(1)
        text("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about\n")
        time.sleep(1)
        key("ret")  # Empty passphrase
        time.sleep(1)
        key("ret")  # Confirm empty passphrase
        time.sleep(2)
        screenshot("review.ppm")
        key("n")
        time.sleep(1)
        text("EXPORT\n")
        time.sleep(3)
        screenshot("account.ppm")
        subprocess.run(["docker", "compose", "run", "--rm", "-v", f"{args.output.resolve()}:/captures:ro",
                        "test", "/build/qr-image-probe", "/captures/account.ppm"], check=True)
        print(f"PASS: boot, local account review and framebuffer QR export; screenshots in {args.output}")
        qmp("quit")
        process.wait(timeout=10)
        stream.close()
        connection.close()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
