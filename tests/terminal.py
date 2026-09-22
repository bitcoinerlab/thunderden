"""Exercise the actual tty review boundary with public fixtures over a pseudo-terminal."""
import errno
import atexit
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shlex
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

probes = []


@atexit.register
def cleanup():
    for p in probes:
        if p.process.poll() is None:
            if p.controlling:
                os.killpg(p.process.pid, signal.SIGKILL)
            else:
                p.process.kill()
            p.process.communicate()
        if p.master >= 0:
            os.close(p.master)
            os.close(p.slave)


class Probe:
    def __init__(self, *args, executable=None, controlling=False, rows=12, tty_output=False):
        self.master, slave = pty.openpty()
        self.slave = slave
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 80, 0, 0))
        self.saved = termios.tcgetattr(slave)
        self.controlling = controlling

        def session():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.process = subprocess.Popen([executable or sys.argv[1], *args], stdin=slave,
                                        stdout=slave if tty_output else subprocess.PIPE, stderr=subprocess.PIPE,
                                        preexec_fn=session if controlling else None)
        self.screen = b""
        self.all_text = b""
        probes.append(self)

    def wait(self, text):
        deadline = time.monotonic() + 5
        while text not in self.screen:
            assert time.monotonic() < deadline, (text, self.screen)
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    data = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                self.screen += data
                self.all_text += data
        assert text in self.screen, (text, self.screen)
        self.screen = self.screen.split(text, 1)[1]

    def send(self, text):
        os.write(self.master, text)

    def finish(self, code):
        output, error = self.process.communicate(timeout=5)
        assert self.process.returncode == code, (self.process.returncode, output, error)
        assert termios.tcgetattr(self.slave) == self.saved, "Terminal settings were not restored"
        while select.select([self.master], [], [], 0)[0]:
            self.all_text += os.read(self.master, 65536)
        os.close(self.master)
        os.close(self.slave)
        self.master = -1
        return output, error


for confirmation, code in [(b"SIGN\r", 0), (b"yes\r", 2), (b"\x1b", 2)]:
    p = Probe()
    p.wait(b"Page 1/3")
    p.send(b"SIGN\r")  # Cannot approve on a review page.
    time.sleep(0.05)
    assert p.process.poll() is None
    p.send(b"nSIGN\r")  # Queued approval text must be discarded at the next page.
    p.wait(b"Page 2/3")
    p.send(b"b")
    p.wait(b"Page 1/3")
    p.send(b"n")
    p.wait(b"Page 2/3")
    p.send(b"n")
    p.wait(b"ADDRESS-END")
    p.wait(b"Page 3/3")
    p.send(b"nSIGN\r")
    p.wait(b"then Enter: ")
    time.sleep(0.05)
    assert p.process.poll() is None, "Buffered input approved transaction"
    p.send(confirmation)
    output, _ = p.finish(code)
    assert (b"APPROVED" in output) == (code == 0)

p = Probe()
p.wait(b"Page 1/3")
p.send(b"q")
p.finish(2)

p = Probe()
p.wait(b"Page 1/3")
fcntl.ioctl(p.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 13, 80, 0, 0))
p.send(b"n")
_, error = p.finish(3)
assert b"Display changed during review" in error

p = Probe("input")
p.wait(b"Secret: ")
p.send(b" A Case  X\x7f \r")
output, _ = p.finish(0)
assert bytes.fromhex(output.decode().strip()) == b" A Case   "
assert b"A Case" not in p.all_text

p = Probe("input")
p.wait(b"Secret: ")
p.send(b"a" * 33)
p.finish(3)

p = Probe("input")
p.wait(b"Secret: ")
p.send("non-ASCII \u00e9".encode())
p.finish(3)
pipe = subprocess.run([sys.argv[1]], input=b"nSIGN\n", capture_output=True, timeout=5)
assert pipe.returncode == 3 and b"Input must be a terminal" in pipe.stderr
print("PASS: tty-only input, full review traversal, explicit consent, queued-input rejection, cancellation, resize and masked ASCII entry")

MNEMONIC = ["abandon"] * 11 + ["about"]


def word(p, index, count, value):
    p.wait(f"Word {index}/{count}: ".encode())
    p.send(value.encode() + b"\r")


def mnemonic(p, words, choice=b"\r"):
    p.wait(b"Enter: 12 words")
    p.send(choice)
    for index, value in enumerate(words, 1):
        word(p, index, len(words), value)


# Exercise the actual application with a controlling tty. The test container has
# no framebuffer; a failed display must return to the menu and preserve the seed
# session, rather than prompt again or export through an alternate channel.
p = Probe(executable=sys.argv[2], controlling=True, rows=24)
p.wait(b"5: Legacy testnet3")
for key in (b"\x1b", b"\x03"):
    for _ in range(5):
        p.send(key)
        time.sleep(0.02)
    assert p.process.poll() is None, "Cancel key ended network selection"
p.send(b"4")
for attempt in range(2):
    p.wait(b"3: End session (clear keys)")
    p.send(b"2")
    p.wait(b"Esc: Cancel")
    p.send(b"1")
    p.wait(b"Account number 0-100 [0]: ")
    p.send(b"\r")
    if attempt == 0:
        mnemonic(p, ["abandon"] * 12)
        p.wait(b"Invalid recovery phrase. Enter all 12 words again.")
        assert b"Passphrase: " not in p.all_text, "Passphrase requested before mnemonic validation"
        for index, value in enumerate(MNEMONIC, 1):
            word(p, index, 12, value)
        p.wait(b"Passphrase: ")
        p.send(b"\r")
    p.wait(b"Page 1/1")
    assert b"Repeat passphrase: " not in p.all_text, "Empty passphrase required confirmation"
    p.send(b"n")
    p.wait(b"Type EXPORT then Enter: ")
    p.send(b"EXPORT\r")
    p.wait(b"framebuffer display is required")
    p.send(b"n")
p.wait(b"3: End session (clear keys)")
for key in (b"\x1b", b"\x03"):
    p.send(b"2")
    p.wait(b"Esc: Cancel")
    p.send(key)
    p.wait(b"3: End session (clear keys)")
    # Autorepeat continues after the next screen has flushed queued input.
    for _ in range(5):
        p.send(key)
        time.sleep(0.02)
    assert p.process.poll() is None, "Repeated cancellation ended the loaded session"
p.send(b"2")
p.wait(b"Esc: Cancel")
p.send(b"1")
p.wait(b"Account number 0-100 [0]: ")
p.send(b"\r")
p.wait(b"Page 1/1")  # The same keys remain usable without re-entering the phrase.
p.send(b"q")
p.wait(b"3: End session (clear keys)")
p.send(b"3")
p.finish(0)
assert p.all_text.count(b"Recovery word count") == 1
assert b"Word number" not in p.all_text
assert b"abandon abandon" not in p.all_text
print("PASS: checksum checked before passphrase, single Enter for empty passphrase and one key session")
print("PASS: repeated Esc/Ctrl-C cancel operations without ending the session; explicit logout still works")

# All standard lengths, with visible words and the same backend checksum checks.
for choice, count, last in [(1, 12, "about"), (2, 15, "address"), (3, 18, "agent"),
                            (4, 21, "admit"), (5, 24, "art")]:
    words = ["abandon"] * (count - 1) + [last]
    p = Probe("mnemonic")
    mnemonic(p, words, str(choice).encode())
    output, _ = p.finish(0)
    assert bytes.fromhex(output.decode().strip()) == " ".join(words).encode()
    assert b"abandon" in p.all_text, "Recovery words were hidden"

# A failed checksum discards all words and retains the chosen length.
p = Probe("mnemonic")
mnemonic(p, ["abandon"] * 24, b"5")
p.wait(b"Invalid recovery phrase. Enter all 24 words again.")
words = ["abandon"] * 23 + ["art"]
for index, value in enumerate(words, 1):
    word(p, index, 24, value)
output, _ = p.finish(0)
assert bytes.fromhex(output.decode().strip()) == " ".join(words).encode()
assert p.all_text.count(b"Recovery word count") == 1

p = Probe("mnemonic")
mnemonic(p, ["abandon"] * 12)
p.wait(b"Invalid recovery phrase. Enter all 12 words again.")
p.wait(b"Word 1/12: ")
p.send(b"\x1b")
output, _ = p.finish(2)
assert not output
print("PASS: invalid phrases restart at word one with the same length and allow cancellation")

p = Probe("mnemonic")
p.wait(b"Enter: 12 words")
p.send(b"\r")
word(p, 1, 12, "zzzz")
p.wait(b"Invalid English recovery word")
word(p, 1, 12, "toolongword")
p.wait(b"Input is too long")
word(p, 1, 12, "ability")
word(p, 2, 12, "")  # Empty entry goes back without losing the earlier word.
p.wait(b"Word 1/12: ")
p.wait(b"ability")
p.send(b"\x7f" * 7 + b"abandon\r")
for index, value in enumerate(MNEMONIC[1:], 2):
    word(p, index, 12, value)
output, _ = p.finish(0)
assert bytes.fromhex(output.decode().strip()) == " ".join(MNEMONIC).encode()

p = Probe("mnemonic")
p.wait(b"Enter: 12 words")
p.send(b"\x1b")
output, _ = p.finish(2)
assert not output

p = Probe("mnemonic")
p.wait(b"Enter: 12 words")
p.send(b"\r")
word(p, 1, 12, "abandon")
p.wait(b"Word 2/12: ")
p.send(b"\x1b")
output, _ = p.finish(2)
assert not output
print("PASS: all mnemonic lengths, visible words, immediate validation, correction and cancellation")

# A mistyped passphrase can be retried without entering the words again.
p = Probe(executable=sys.argv[2], controlling=True, rows=24)
p.wait(b"5: Legacy testnet3")
p.send(b"4")
p.wait(b"3: End session (clear keys)")
p.send(b"2")
p.wait(b"Esc: Cancel")
p.send(b"1")
p.wait(b"Account number 0-100 [0]: ")
p.send(b"\r")
mnemonic(p, MNEMONIC)
p.wait(b"Passphrase: ")
p.send(b" A Case  \r")
p.wait(b"Repeat passphrase: ")
p.send(b" A Case\r")
p.wait(b"Page 1/1")
assert b"Passphrases did not match" in p.all_text
p.send(b"n")
p.wait(b"Passphrase: ")
p.send(b" A Case  \r")
p.wait(b"Repeat passphrase: ")
p.send(b" A Case  \r")
p.wait(b"Page 1/1")
p.send(b"q")
p.wait(b"3: End session (clear keys)")
p.send(b"3")
p.finish(0)
assert p.all_text.count(b"Recovery word count") == 1
assert b" A Case" not in p.all_text, "Passphrase was displayed"
print("PASS: non-empty passphrase confirmation, exact spaces/case and retry without re-entering words")

# Run the real appliance launcher with fixture mount tables and the native signer.
# All session control remains in the production script; poweroff is redirected so
# a regression cannot request shutdown on the test machine.
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    mounts, swaps, launcher = (root / name for name in ("mounts", "swaps", "session"))
    mounts.write_text("rootfs / rootfs rw 0 0\ntmpfs /tmp tmpfs rw 0 0\ntmpfs /run tmpfs rw 0 0\n")
    swaps.write_text("Filename Type Size Used Priority\n")
    script = (Path(__file__).resolve().parents[1] / "platform/overlay/usr/bin/thunderden-session").read_text()
    for original, replacement in [("/proc/mounts", mounts), ("/proc/swaps", swaps),
                                  ("/sbin/poweroff", root / "unexpected-poweroff")]:
        script = script.replace(original, shlex.quote(str(replacement)))
    launcher.write_text(script.replace("/usr/bin/thunderden-signer", shlex.quote(str(Path(sys.argv[2]).resolve()))))
    p = Probe(str(launcher), executable="/bin/sh", controlling=True, rows=24, tty_output=True)
    children = Path(f"/proc/{p.process.pid}/task/{p.process.pid}/children")
    pids, fingerprints = [], []
    for words, passphrase, network in [(MNEMONIC, b"", b"4"), (["all"] * 12, b"TREZOR", b"1")]:
        p.wait(b"5: Legacy testnet3")
        pid, = children.read_text().split()
        assert pid not in pids, "Session reused the previous signer process"
        pids.append(pid)
        p.send(network)
        p.wait(b"Thunder Den - " + (b"regtest" if network == b"4" else b"testnet4"))
        p.wait(b"3: End session (clear keys)")
        p.send(b"2")
        p.wait(b"Esc: Cancel")
        p.send(b"1")
        p.wait(b"Account number 0-100 [0]: ")
        p.send(b"\r")
        mnemonic(p, words)
        p.wait(b"Passphrase: ")
        p.send(passphrase + b"\r")
        if passphrase:
            p.wait(b"Repeat passphrase: ")
            p.send(passphrase + b"\r")
        p.wait(b"Page 1/1")
        fingerprints.append(re.findall(rb"Signer fingerprint: ([0-9a-f]{8})", p.all_text)[-1])
        p.send(b"q")
        p.wait(b"3: End session (clear keys)")
        p.send(b"3")
        p.wait(b"Session ended\r\nLoaded keys cleared.")
        p.wait(b"You can now turn off the laptop.")
        p.wait(b"Enter: Start a new session")
        assert not children.read_text().strip(), "Completion appeared before the signer exited"
        assert not Path(f"/proc/{pid}").exists(), "Old signer is still alive"
        p.send(b"x")
        time.sleep(0.05)
        assert not children.read_text().strip(), "Session restarted without Enter"
        if len(pids) == 1:
            p.send(b"\r")
    assert fingerprints[0] != fingerprints[1], "New seed reused the old wallet"
    assert p.all_text.count(b"Recovery word count") == 2
    assert b"TREZOR" not in p.all_text
    p.send(b"\x15\x04")  # Clear the pending line, then end input at the logout screen.
    p.wait(b"Signer stopped.")
    assert not children.read_text().strip(), "End of input restarted the signer"
    p.process.terminate()
    p.finish(-signal.SIGTERM)

    launcher.write_text(script.replace("/usr/bin/thunderden-signer", "/bin/false"))
    p = Probe(str(launcher), executable="/bin/sh", controlling=True, tty_output=True)
    p.wait(b"Signer stopped.")
    assert b"Loaded keys cleared" not in p.all_text, "Failed signer reported successful cleanup"
    p.process.terminate()
    p.finish(-signal.SIGTERM)
print("PASS: logout waits for process exit, stays idle until Enter and starts fresh keys, passphrase and network")
