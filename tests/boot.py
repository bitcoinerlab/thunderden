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
parser.add_argument("--display-size", help="QEMU display size for font/layout checks, such as 640x480")
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
if args.display_size:
    width, height = map(int, args.display_size.split("x"))
    command += ["-vga", "none", "-device",
                f"VGA,xres={width},yres={height},xmax={width},ymax={height},vgamem_mb=64"]
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

        def key(value, hold=40):
            qmp("human-monitor-command", {"command-line": "sendkey " + value + " " + str(hold)})
            time.sleep(0.07)

        def text(value):
            for char in value:
                key("spc" if char == " " else "ret" if char == "\n" else "shift-" + char.lower() if char.isupper() else char)

        def screenshot(name):
            qmp("screendump", {"filename": str((args.output / name).resolve())})

        qmp("qmp_capabilities")
        time.sleep(25)
        screenshot("network.ppm")
        for _ in range(3):
            key("down")
        key("ret")  # Regtest, selected using arrow keys.
        time.sleep(2)
        screenshot("menu.ppm")
        key("down")
        key("ret")  # Share wallet setup.
        time.sleep(1)
        key("1")  # BIP84
        time.sleep(1)
        key("ret")  # Account zero
        time.sleep(1)
        key("ret")  # Default: 12 recovery words
        time.sleep(1)
        screenshot("recovery-entry.ppm")
        text("zzzz\n")
        time.sleep(0.3)
        screenshot("invalid-word.ppm")
        for word in ["abandon"] * 11 + ["about"]:
            text(word + "\n")
        time.sleep(1)
        screenshot("passphrase.ppm")
        key("ret")  # Empty passphrase
        time.sleep(2)
        screenshot("review.ppm")
        key("d")
        time.sleep(0.3)
        screenshot("details.ppm")
        key("d")
        time.sleep(0.3)
        key("ret")  # Show the public descriptor directly.
        time.sleep(3)
        screenshot("account.ppm")
        subprocess.run(["docker", "compose", "run", "--rm", "-v", f"{args.output.resolve()}:/captures:ro",
                         "test", "/build/qr-image-probe", "/captures/account.ppm"], check=True)
        key("esc", 1500)  # A held key must only leave the QR screen, not end the session.
        time.sleep(2)
        screenshot("menu-after-escape.ppm")
        key("2")
        time.sleep(0.5)
        key("1")
        time.sleep(0.5)
        key("ret")  # Account zero. The existing recovery-word session must remain.
        time.sleep(1)
        key("ret")  # Show the descriptor again without re-entering recovery words.
        time.sleep(2)
        screenshot("account-after-escape.ppm")
        subprocess.run(["docker", "compose", "run", "--rm", "-v", f"{args.output.resolve()}:/captures:ro",
                         "test", "/build/qr-image-probe", "/captures/account-after-escape.ppm"], check=True)
        print(f"PASS: boot, arrow menus, full QR export and held-Escape session preservation; screenshots in {args.output}")
        qmp("quit")
        process.wait(timeout=10)
        stream.close()
        connection.close()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
