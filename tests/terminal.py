"""Exercise the actual tty review boundary with public fixtures over a pseudo-terminal."""
import errno
import atexit
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time

probes = []


@atexit.register
def cleanup():
    for p in probes:
        if p.process.poll() is None:
            p.process.kill()
            p.process.communicate()
        if p.master >= 0:
            os.close(p.master)
            os.close(p.slave)


class Probe:
    def __init__(self, *args, executable=None, controlling=False, rows=12):
        self.master, slave = pty.openpty()
        self.slave = slave
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 80, 0, 0))
        self.saved = termios.tcgetattr(slave)

        def session():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.process = subprocess.Popen([executable or sys.argv[1], *args], stdin=slave,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
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

# Exercise the actual application with a controlling tty. The test container has
# no framebuffer; a failed display must return to the menu and preserve the seed
# session, rather than prompt again or export through an alternate channel.
p = Probe(executable=sys.argv[2], controlling=True, rows=24)
p.wait(b"Esc: End session")
p.send(b"4")
for attempt in range(2):
    p.wait(b"3: End session")
    p.send(b"2")
    p.wait(b"Esc: Cancel")
    p.send(b"1")
    p.wait(b"Account number 0-100 [0]: ")
    p.send(b"\r")
    if attempt == 0:
        p.wait(b"Recovery words: ")
        p.send(b"abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about\r")
        p.wait(b"Passphrase: ")
        p.send(b"\r")
        p.wait(b"Repeat passphrase: ")
        p.send(b"\r")
    p.wait(b"Page 1/1")
    p.send(b"n")
    p.wait(b"Type EXPORT then Enter: ")
    p.send(b"EXPORT\r")
    p.wait(b"framebuffer display is required")
    p.send(b"n")
p.wait(b"3: End session")
p.send(b"3")
p.finish(0)
assert p.all_text.count(b"Recovery words: ") == 1
assert b"abandon abandon" not in p.all_text
print("PASS: native application menu, one seed entry per session and recovery from unavailable display")
