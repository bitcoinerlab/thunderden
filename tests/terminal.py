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
    def __init__(self, *args, executable=None, controlling=False, rows=24, columns=80, tty_output=False):
        self.master, slave = pty.openpty()
        self.slave = slave
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
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

    def wait(self, text, timeout=5):
        deadline = time.monotonic() + timeout
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

    def enter(self, prompt, text):
        self.wait(prompt)
        self.send(text)

    def rows(self):
        # Inspect positioned text in the current screen; these tests use ASCII
        # fixtures only. Ignore color/line-clear sequences, not cursor positions.
        screen = self.all_text.rsplit(b"\x1b[2J", 1)[-1]
        return {int(row): text for row, text in re.findall(
            rb"\x1b\[(\d+);\d+H(?:\x1b\[[0-9;]*[mK])*([^\x1b\r\n]*)", screen)}

    def spaced(self, prefix):
        rows = self.rows()
        row, = [row for row, text in rows.items() if text.startswith(prefix)]
        assert not rows.get(row - 1, b"").strip(), ("Missing blank row above", prefix, rows)

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

    def finish_secret(self, expected):
        output, _ = self.finish(0)
        assert bytes.fromhex(output.decode().strip()) == expected


for confirmation, code in [(b"SIGN\r", 0), (b"yes\r", 2), (b"\x1b", 2)]:
    p = Probe(rows=13)
    p.wait(b"Page 1/4")
    p.send(b"SIGN\r")  # Cannot approve on a review page.
    time.sleep(0.05)
    assert p.process.poll() is None
    p.send(b"nSIGN\r")  # Queued approval text must be discarded at the next page.
    p.wait(b"Page 2/4")
    p.send(b"b")
    p.wait(b"Page 1/4")
    p.send(b"n")
    p.wait(b"Page 2/4")
    p.send(b"n")
    p.wait(b"Page 3/4")
    p.send(b"\x1b[C")
    p.wait(b"ADDRESS-END")
    p.wait(b"Page 4/4")
    p.send(b"nSIGN\r")
    p.wait(b"then Enter: ")
    p.spaced(b"Type SIGN")
    time.sleep(0.05)
    assert p.process.poll() is None, "Buffered input approved transaction"
    p.send(confirmation)
    output, _ = p.finish(code)
    assert (b"APPROVED" in output) == (code == 0)

p = Probe(rows=13)
p.wait(b"Page 1/4")
p.send(b"q")
p.finish(2)

p = Probe(rows=13)
p.wait(b"Page 1/4")
fcntl.ioctl(p.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 14, 80, 0, 0))
p.send(b"n")
_, error = p.finish(3)
assert b"Display changed during review" in error

p = Probe("details")
p.wait(b"Press Enter to finish.")
fcntl.ioctl(p.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 25, 80, 0, 0))
p.send(b"\r")
_, error = p.finish(3)
assert b"Display changed during review" in error, "Resize on the last page allowed approval"

p = Probe("input")
p.wait(b"Secret: ")
p.send(b"a")
fcntl.ioctl(p.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 25, 80, 0, 0))
p.send(b"\r")
output, error = p.finish(3)
assert not output and b"screen changed" in error, "Resized input was accepted"

p = Probe("input")
p.wait(b"Secret: ")
p.spaced(b"Secret:")
assert p.rows()[2] == b"THUNDER DEN", "Missing top margin"
assert list(p.rows().values()).index(b"Enter public test data") < list(p.rows().values()).index(b"Your passphrase is hidden as you type.")
assert p.rows()[9] == b"Press TAB if you want to see what you are entering.", "TAB hint must follow the status on its own line"
assert b"Press Enter to continue." not in p.all_text, "Redundant instruction in a text input"
p.send(b" A Case  X\x7f \r")
p.finish_secret(b" A Case   ")
assert b"A Case" not in p.all_text

p = Probe("input")
p.wait(b"Secret: ")
p.send(b"a" * 33 + b"\r")
p.wait(b"This entry is too long.")
p.wait(b"Secret: ")
p.send(b"valid\r")
p.finish_secret(b"valid")

p = Probe("input")
p.wait(b"Secret: ")
p.send("non-ASCII \u00e9".encode())
p.finish(3)
pipe = subprocess.run([sys.argv[1]], input=b"nSIGN\n", capture_output=True, timeout=5)
assert pipe.returncode == 3 and b"Input must be a terminal" in pipe.stderr
print("PASS: tty-only input, full review traversal, explicit consent, queued-input rejection, cancellation, resize and masked ASCII entry")

for columns in (40, 80, 160):
    p = Probe("menu", rows=14, columns=columns)
    p.wait(b"Enter/1-3: Select")
    p.send(b"\x1b[B")
    p.wait(b"> 2: Second action")
    assert p.all_text.count(b"\x1b[2J") == 1, "Menu navigation cleared the screen"
    p.send(b"\x1bOB")
    p.wait(b"> 3: Third action")
    p.send(b"\x1b[A")
    p.wait(b"> 2: Second action")
    p.send(b"\r")
    output, _ = p.finish(0)
    assert output.strip() == b"1", "Arrows selected the wrong menu item"

for rows in (12, 24):
    p = Probe("menu-loaded", rows=rows, columns=40)
    p.wait(b"\x1b[33mLoaded wallet:")
    p.send(b"\x1b[B")
    p.wait(b"> 2: Nested SegWit multisig")
    p.wait(b"(P2SH-P2WSH)")
    assert p.all_text.count(b"\x1b[2J") == 1, "Wrapped-menu navigation cleared the screen"
    p.send(b"\r")
    output, _ = p.finish(0)
    assert output.strip() == b"1"

p = Probe("menu", rows=12, columns=40)
p.wait(b"Enter/1-3: Select")
for option in (2, 3):
    p.send(b"\x1b[B")
    p.wait(f"> {option}: ".encode())
assert p.all_text.count(b"\x1b[2J") == 1, "Scrolling a menu cleared the whole screen"
p.send(b"\r")
p.finish(0)

p = Probe("input")
p.wait(b"Secret: ")
p.send(b"ab\x1b[D\x1b[15~\x1b[[Acd\r")
p.finish_secret(b"abcd")

p = Probe("input", rows=16, columns=40)
p.wait(b"Secret: ")
p.send(b" A Case with spaces " + b"x" * 12 + b"\x7f" * 12 + b" \r")
p.finish_secret(b" A Case with spaces  ")
assert b"A Case" not in p.all_text, "A long entry exposed the passphrase"

p = Probe("input")
p.wait(b"Secret: ")
p.send(b" A Case ")
p.wait(b"*" * len(b" A Case "))
assert b"A Case" not in p.all_text
p.send(b"\t")
p.wait(b"Your passphrase is visible as you type.")
p.wait(b" A Case ")
assert p.rows()[6] == b"Enter public test data" and p.rows()[9] == b"Press TAB to hide it.", "Visibility toggle overwrote the introduction"
for visible in (False, True, False, True):
    p.send(b"\t")
    p.wait(b"Your passphrase is visible" if visible else b"Your passphrase is hidden")
p.send(b"\t\t")  # Both events must work without a cooldown, even in one write.
p.wait(b"Your passphrase is hidden")
p.wait(b"Your passphrase is visible")
p.send(b"X")
p.wait(b" A Case X")
p.send(b"\t")
p.wait(b"Your passphrase is hidden as you type")
p.wait(b"*" * len(b" A Case X"))
hidden = len(p.all_text)
p.send(b" \r")
p.finish_secret(b" A Case X ")
assert b"A Case" not in p.all_text[hidden:], "Hidden text remained in the input renderer"

# Introductory text can be paged on a very small display without losing the
# visibility control or the blank row above the active field.
p = Probe("input", rows=12, columns=40)
p.wait(b"Press Enter to continue.")
p.spaced(b"Press Enter to continue.")
p.send(b"\r")
p.wait(b"Secret: ")
p.spaced(b"Secret:")
p.send(b"tiny\r")
p.finish_secret(b"tiny")

# Details share the primary action and cancel the whole operation with Escape.
for cancel in (b"\x1b", b"\x03"):
    p = Probe("details", rows=12, columns=40)
    p.wait(b"d: Details")
    p.send(b"d")
    p.wait(b"Technical details")
    p.wait(b"d: Summary")
    p.send(cancel)
    p.finish(2)

p = Probe("details")
p.enter(b"d: Details", b"d\r")  # Queued Enter cannot approve while changing views.
p.wait(b"d: Summary")
assert p.process.poll() is None, "Opening details consumed queued approval"
assert b"Page 1/1" not in p.all_text and b"Enter: Back" not in p.all_text
p.send(b"\r")
p.finish(0)
print("PASS: arrow menus, safe input sequences and shared details approval/cancellation")

p = Probe("details-paged", rows=13)
p.wait(b"Page 1/3")
p.send(b"n")
p.wait(b"Page 2/3")
assert re.search(rb"\x1b\[11;([6-9][0-9])H Page 2/3 ", p.all_text), "Pager is not right-aligned above the actions"
p.send(b"d")
p.wait(b"Page 1/3")
p.wait(b"d: Summary")
p.send(b"d")
p.wait(b"Page 2/3")
assert p.process.poll() is None, "Closing details approved the operation"
p.send(b"d")
p.wait(b"Page 1/3")
for page in (2, 3):
    p.send(b"\r")
    p.wait(f"Page {page}/3".encode())
    assert p.process.poll() is None, "Details bypassed the remaining review"
p.send(b"n")
p.wait(b"Page 3/3")
assert p.process.poll() is None, "Next page approved the export"
p.send(b"\r")
p.finish(0)
print("PASS: details preserve summary position and require full expanded review before approval")

# Technical Details do not repeat the summary or authorize signing. Returning
# by either route restores the required review without skipping any pages.
for read_details in (False, True):
    p = Probe("approve-details", rows=13)
    p.enter(b"Page 1/4", b"n")
    p.enter(b"Page 2/4", b"dSIGN\r")
    p.wait(b"d: Summary")
    assert b"Review field" not in p.all_text.rsplit(b"\x1b[2J", 1)[-1], "Details repeated the summary"
    assert p.process.poll() is None, "Opening details consumed queued approval"
    if read_details:
        p.send(b"\r")
        p.wait(b"DESCRIPTOR-END")
        p.wait(b"Press Enter to return to the review.")
        p.send(b"nSIGN\r")
        p.wait(b"Press Enter to return to the review.")
        assert p.process.poll() is None, "Next on the final Details page authorized signing"
        p.send(b"\r")
    else:
        p.send(b"d")
    p.wait(b"Page 2/4")
    for page in (3, 4):
        p.send(b"\r")
        p.wait(f"Page {page}/4".encode())
        assert p.process.poll() is None, "Details bypassed the remaining approval review"
    assert b"ADDRESS-END" in p.all_text, "Approval skipped the complete address"
    p.send(b"nSIGN\r")
    p.wait(b"then Enter: ")
    time.sleep(0.05)
    assert p.process.poll() is None, "Leaving details consumed queued approval"
    p.send(b"SIGN\r")
    p.finish(0)

p = Probe("approve-details", rows=13)
p.enter(b"d: Details", b"d")
p.enter(b"d: Summary", b"\x1b")
p.finish(2)
print("PASS: approval details preserve required pages, complete values, typed consent and cancellation")

for finish in (False, True):
    p = Probe("completed-review", rows=13)
    p.wait(b"Signed transaction - review")
    p.enter(b"Esc: Finish", b"d")
    p.wait(b"Wallet ID:")
    p.wait(b"d: Summary")
    assert b"Approved payment" not in p.all_text.rsplit(b"\x1b[2J", 1)[-1]
    if finish:
        p.send(b"\x1b")
        output, _ = p.finish(2)
        assert b"FINISHED" in output
    else:
        p.send(b"d")
        p.wait(b"ADDRESS-END")
        p.enter(b"Enter: show QR again", b"\r")
        output, _ = p.finish(0)
        assert b"SHOW QR AGAIN" in output
    assert b"Type SIGN" not in p.all_text, "Revisiting a result asked to sign again"
print("PASS: completed-result inspection uses Show QR again and Finish without another approval")

def approve_review(p, word=b"SIGN"):
    while True:
        p.wait(b"Esc:")
        last = p.all_text.rsplit(b"\x1b[2J", 1)[-1]
        p.send(b"\r")
        if b"Enter: continue" in last:
            break
    p.enter(b"Type " + word + b" then Enter: ", word + b"\r")


p = Probe("workflow-one", rows=40)
p.wait(b"Review transaction")
approve_review(p)
output, _ = p.finish(0)
assert b"EMPTY SIGNED" in output
assert b"Choose a wallet to sign with" not in p.all_text and b"Choose an address type" not in p.all_text

for columns in (40, 80):
    p = Probe("workflow-many", rows=40, columns=columns)
    p.enter(b"Enter/1-2: Select", b"1")
    p.wait(b"Review transaction")
    p.enter(b"Esc:", b"\x1b")
    p.enter(b"Enter/1-2: Select", b"2")
    p.wait(b"Account: 1")
    approve_review(p)
    output, _ = p.finish(0)
    assert b"EMPTY SIGNED" in output

for mode in ("workflow-inline", "workflow-inline-text"):
    for accept_setup, sign in ((False, False), (True, False), (True, True)):
        p = Probe(mode, rows=40)
        p.enter(b"Enter: scan wallet setup", b"\r")
        p.wait(b"Review wallet setup")
        if accept_setup:
            approve_review(p, b"REGISTER")
            p.wait(b"Review transaction")
            if sign:
                approve_review(p)
            else:
                p.enter(b"Esc: Cancel", b"\x1b")
        else:
            p.enter(b"Esc: Cancel", b"\x1b")
        output, _ = p.finish(0)
        assert (b"LOADED" in output) == accept_setup
        assert (b"SIGNED" in output) == sign
        assert p.all_text.count(b"Wallet setup needed") == 1
        if accept_setup:
            assert b"You'll still review and approve each transaction before signing." in p.all_text

for mode in ("workflow-preload", "workflow-preload-text"):
    for accept_setup in (False, True):
        p = Probe(mode, rows=40)
        p.wait(b"Review wallet setup")
        if accept_setup:
            approve_review(p, b"REGISTER")
            p.wait(b"\x1b[33mLoaded wallet: Native SegWit multisig (2 of 2)")
            p.send(b"1")
            p.wait(b"Review transaction")
            approve_review(p)
        else:
            p.enter(b"Esc: Cancel", b"\x1b")
        output, _ = p.finish(0)
        assert (b"LOADED SIGNED" if accept_setup else b"EMPTY CANCELLED") in output
        assert b"Wallet setup needed" not in p.all_text

for accept_setup, sign in ((False, False), (True, False), (True, True)):
    p = Probe("workflow-inferred", rows=40)
    p.wait(b"The scanned transaction includes a multisig wallet setup.")
    if accept_setup:
        approve_review(p, b"REGISTER")
        p.wait(b"Review transaction")
        if sign:
            approve_review(p)
        else:
            p.enter(b"Esc: Cancel", b"\x1b")
        assert b"You'll still review and approve each transaction before signing." in p.all_text
    else:
        p.enter(b"Esc: Cancel", b"\x1b")
    output, _ = p.finish(0)
    assert (b"LOADED" in output) == accept_setup
    assert (b"SIGNED" in output) == sign
    assert b"Wallet setup needed" not in p.all_text

p = Probe("workflow-unmatched", rows=40)
p.wait(b"No matching signing key")
p.enter(b"Esc: Cancel", b"\x1b")
output, _ = p.finish(0)
assert b"EMPTY CANCELLED" in output and b"Wallet setup needed" not in p.all_text

for mode in ("workflow-replace", "workflow-replace-text"):
    for replace in (False, True):
        p = Probe(mode, rows=40)
        p.wait(b"Switch wallet setup")
        if replace:
            approve_review(p, b"REGISTER")
        else:
            p.enter(b"Esc: Cancel", b"\x1b")
        output, _ = p.finish(0)
        assert (b"REPLACED" if replace else b"KEPT") in output

p = Probe("workflow-replace-invalid-text", rows=40)
output, _ = p.finish(0)
assert b"KEPT" in output and b"Switch wallet setup" not in p.all_text

for stop in ("warning", "review", "sign"):
    p = Probe("workflow-fee", rows=40)
    p.wait(b"Wallet apps such as Sparrow")
    p.wait(b"Enter: continue to review")
    assert b"\x1b[0;31;40mFee not fully verified" in p.all_text
    if stop == "warning":
        p.send(b"\x1b")
    else:
        p.send(b"n")  # Navigation alone must not consent on the warning's last page.
        time.sleep(0.03)
        assert p.process.poll() is None and b"Unverified fee:" not in p.all_text
        p.send(b"\r")
        p.wait(b"Unverified fee:")
        if stop == "review":
            p.enter(b"Esc: Cancel", b"\x1b")
        else:
            p.enter(b"Esc: Cancel", b"d")
            p.wait(b"Wallet ID:")
            p.enter(b"Esc: Cancel", b"d")
            p.wait(b"Unverified fee:")
            approve_review(p)
            p.enter(b"Esc: Finish", b"\x1b")
            assert len(re.findall(rb"\x1b\[0;31;40m(?:Review - |Signed - )?[Ff]ee not fully verified", p.all_text)) >= 5
            assert b"The fee uses unverified input amounts." in p.all_text
    output, _ = p.finish(0)
    assert (b"SIGNED" in output) == (stop == "sign")
print("PASS: automatic signing, chooser/back, inline setup, atomic replacement and persistent fee-warning consent")

for columns in (40, 80):
    for fee_unverified in (False, True):
        for stop in ("review", "confirmation", "sign"):
            p = Probe("workflow-change-fee" if fee_unverified else "workflow-change", rows=16, columns=columns)
            if fee_unverified:
                p.wait(b"Fee not fully verified")
                while True:
                    p.wait(b"Esc:")
                    last = p.all_text.rsplit(b"\x1b[2J", 1)[-1]
                    p.send(b"\r")
                    if b"Enter: continue to review" in last:
                        break
            title = b"Review - fee not fully verified" if fee_unverified else b"Review - high change index"
            p.wait(b"\x1b[0;31;40m" + title)
            if stop == "review":
                p.enter(b"Esc:", b"\x1b")
            else:
                while True:
                    p.wait(b"Esc:")
                    last = p.all_text.rsplit(b"\x1b[2J", 1)[-1]
                    assert b"\x1b[0;31;40m" + title in last, "Recovery warning lost its red review heading"
                    assert b"Type SIGN" not in p.all_text, "Approval appeared before completing mandatory review"
                    p.send(b"\r")
                    if b"Enter: continue" in last:
                        break
                p.wait(b"Type SIGN then Enter: ")
                assert b"Change address index: 1000000000" in p.all_text
                assert b"WARNING: Unusually high change index." in p.all_text
                confirmation = b" ".join(p.rows().values())
                assert b"miss" in confirmation and b"recovery" in confirmation, "Final confirmation lost the recovery risk"
                if fee_unverified:
                    assert b"accept both risks" in confirmation
                else:
                    assert b"Unverified fee:" not in p.all_text and b"unverified input amounts" not in confirmation
                p.send(b"SIGN\r" if stop == "sign" else b"\x1b")
                if stop == "sign":
                    signed_title = b"Signed - fee not fully verified" if fee_unverified else b"Signed - high change index"
                    p.wait(b"\x1b[0;31;40m" + signed_title)
                    p.enter(b"Esc:", b"\x1b")
            output, _ = p.finish(0)
            assert (b"EMPTY SIGNED" in output) == (stop == "sign")
            if stop != "review":
                address = re.search(rb"CHANGE (\S+)", output)[1]
                rendered = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", p.all_text)
                assert address in rendered, "Mandatory review did not display the full high-index address"
print("PASS: high-index change warning, full address, typed approval, cancellation and combined fee/recovery risks on narrow/wide screens")


def address_action(p, action, key=b"\r"):
    # Compact screens page required context before showing a menu/confirmation.
    while True:
        p.wait(b"Esc:")
        last = p.all_text.rsplit(b"\x1b[2J", 1)[-1]
        if action in last:
            p.send(key)
            return
        assert b"Press Enter" in last, (action, last)
        p.send(b"\r")


def address_number(p, prompt=b"Account number [0]: ", value=b"\r"):
    while True:
        p.wait(b"Esc:")
        if b"Enter: Continue" in p.all_text.rsplit(b"\x1b[2J", 1)[-1]:
            break
        p.send(b"\r")
    p.wait(prompt)
    p.spaced(prompt.rstrip())
    if prompt.startswith(b"Account"):
        assert b"Most wallets use account 0." in b" ".join(p.rows().values()), "Account help disappeared from the input screen"
        p.spaced(b"Most wallets use account 0.")
    p.send(value)


def finish_address(p):
    p.wait(b"Address verified")
    while True:
        p.wait(b"Esc:")
        last = p.all_text.rsplit(b"\x1b[2J", 1)[-1]
        p.send(b"\r")
        if b"Enter: finish" in last:
            break
    output, _ = p.finish(0)
    address = re.search(rb"EXPECTED (\S+)", output)[1]
    rendered = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", p.all_text)
    assert address in rendered and b"Before sending," in rendered, "Verified address was truncated or comparison instruction missing"
    assert b"CHECK FINISHED" in output and b"Type SIGN" not in p.all_text
    assert b"amount=0.1" not in p.all_text and b"Public%20test" not in p.all_text, "Payment URI text was treated as review data"


for rows, columns in ((12, 40), (24, 80)):
    for mode, branch in (("address-single", b"Receiving"), ("address-change", b"Change"), ("address-incompatible", b"Receiving")):
        p = Probe(mode, rows=rows, columns=columns)
        address_number(p)
        finish_address(p)
        assert branch + b" address number: 7" in p.all_text and b"Which wallet" not in p.all_text
        assert b"Single-signature" not in p.all_text, "Ordinary address checks exposed an unnecessary wallet-type choice"

p = Probe("address-single")
address_number(p, value=b"abc\r")
p.wait(b"Enter a number from 0 to 100.")
address_number(p, value=b"101\r")
p.wait(b"Enter a number from 0 to 100.")
address_number(p)
finish_address(p)

for mode, action in (("address-account", b"2"), ("address-late", b"1"), ("address-high", b"3")):
    p = Probe(mode, rows=16, columns=40)
    address_number(p)
    p.wait(b"No match found")
    address_action(p, b"Enter/1-3: Select", action)
    assert p.all_text.count(b"0-999") == 1, "Search range was hidden or repeated"
    if mode == "address-account":
        address_number(p, value=b"7\r")
    elif mode == "address-high":
        address_number(p, b"Address number [0]: ", b"2147483648\r")
        p.wait(b"Enter a number from 0 to 2147483647.")
        address_number(p, b"Address number [0]: ", b"2147483647\r")
    finish_address(p)
    assert b"No match in all" not in p.all_text

for rows, columns in ((12, 40), (24, 80)):
    p = Probe("address-wrong", rows=rows, columns=columns)
    address_number(p)
    p.wait(b"No match found")
    address_action(p, b"Enter/1-3: Select", b"3")
    assert p.all_text.count(b"0-999") == 1, "Compact search result lost or duplicated the checked range"
    address_number(p, b"Address number [0]: ", b"7\r")
    p.wait(b"No match found")
    address_action(p, b"Enter/1-3: Select", b"\x1b")
    p.finish(2)
    assert b"number 7." in p.all_text and b"7-7" not in p.all_text
    assert b"Address verified" not in p.all_text and b"Wallet setup needed" not in p.all_text

for mode in ("address-multisig", "address-multisig-change"):
    p = Probe(mode, rows=12, columns=40)
    address_action(p, b"Enter: verify address")
    finish_address(p)
    assert b"Account number [0]" not in p.all_text and b"Type REGISTER" not in p.all_text
    assert (b"Change" if mode.endswith("change") else b"Receiving") + b" address number: 7" in p.all_text

for rows, columns in ((12, 40), (24, 80)):
    for mode, choice in (("address-ambiguous", b"1"), ("address-ambiguous-single", b"2")):
        p = Probe(mode, rows=rows, columns=columns)
        p.wait(b"Which wallet should we verify against?")
        p.enter(b"Enter/1-2: Select", choice)
        if choice == b"2":
            address_number(p)
        finish_address(p)

p = Probe("address-unloaded")
p.wait(b"Wallet setup needed")
address_action(p, b"Enter: go back")
p.finish(0)
assert b"Checking address number" not in p.all_text
p = Probe("address-unapproved")
_, error = p.finish(3)
assert b"not approved for this seed" in error and b"Address verified" not in p.all_text

# Twenty-key public fixture makes the search long enough to observe incremental
# updates. It exercises the same synchronous search used on the device.
for resize in (False, True):
    p = Probe("address-progress", rows=16, columns=40)
    address_action(p, b"Enter: verify address")
    p.wait(b"Checking address number:")
    start = p.all_text.rfind(b"\x1b[2J")
    p.wait(b"\x1b[13;30H")
    if resize:
        fcntl.ioctl(p.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 16, 80, 0, 0))
        _, error = p.finish(3)
        assert b"screen changed" in error
        continue
    p.wait(b"\x1b[13;30H")
    p.send(b"\x1b")
    p.wait(b"Address check paused")
    search = p.all_text[start:].rsplit(b"\x1b[2J", 1)[0]
    assert search.count(b"\x1b[2J") == 1 and search.count(b"Receiving and change addresses") == 1, "Search repainted fixed text"
    numbers = [int(n) for n in re.findall(rb"\x1b\[13;30H *(\d+)", search)]
    assert len(numbers) >= 2 and numbers[0] == 0 and numbers[-1] > 0, "Index did not advance in place"
    address_action(p, b"Enter/1-2: Select", b"2")
    address_number(p, b"Address number [0]: ", b"\x1b")
    p.wait(b"Address check paused")
    address_action(p, b"Enter/1-2: Select", b"1")
    p.wait(b"Checking address number:")
    p.wait(b"\x1b[13;30H")
    p.send(b"\x1b")
    p.wait(b"Address check paused")
    resumed = [int(n) for n in re.findall(rb"\x1b\[13;30H *(\d+)", p.all_text)]
    assert resumed[-1] >= numbers[-1], "Resume reset the address search"
    address_action(p, b"Enter/1-2: Select", b"q")
    # Menu cancellation is Esc/Ctrl-C, not arbitrary text; a fresh non-cancel key
    # above releases the held-Escape latch before leaving the stopped menu.
    p.send(b"\x1b")
    p.finish(2)
print("PASS: address account selection, both branches, multisig, ambiguity, bounded search, exact indices and non-flickering stop/resume")

MNEMONIC = ["abandon"] * 11 + ["about"]


def word(p, index, count, value):
    p.enter(f"Word {index} of {count}: ".encode(), value.encode() + b"\r")


def mnemonic(p, words, choice=b"\r"):
    if p.controlling:
        p.enter(b"Use existing recovery words", b"1")
    p.enter(b"1: 12 words", choice)
    for index, value in enumerate(words, 1):
        word(p, index, len(words), value)


def public_key_export(p):
    p.send(b"2")
    p.enter(b"Esc: Back", b"1")
    p.enter(b"Account number 0-100 [0]: ", b"\r")


def export_review(p):
    while True:
        p.wait(b"Esc:")
        if b"Enter: show the QR code" in p.all_text.rsplit(b"\x1b[2J", 1)[-1]:
            return
        p.send(b"\r")


for mode, entered, expected in (
        ("entropy-coins", b"h" * 149 + b"t\x7fhT", bytes(150)),
        ("entropy-coins", b"yY" + b"hHtT" * 37 + b"hThH", bytes([0,0,1,1]) * 37 + bytes([0,1])),
        ("entropy-dice", b"07" + b"6" * 63 + b"5\x7f1", bytes([5] * 63 + [0]))):
    p = Probe(mode, rows=12, columns=40)
    address_action(p, b"Enter: start collecting")
    p.wait(b"Tosses entered:" if mode != "entropy-dice" else b"Rolls entered:")
    if mode == "entropy-coins":
        assert b"H: Heads   T: Tails" in p.all_text, "Coin labels do not match the input keys"
    p.send(entered)
    p.wait(b"Enter: Continue")
    assert p.process.poll() is None, "Reaching the manual target skipped confirmation"
    p.send(b"\r")
    output, _ = p.finish(0)
    assert bytes.fromhex(output.decode().strip()) == expected


def start_generation(p, words=12, camera=False, manual=b"1"):
    address_action(p, b"Enter/1-2: Select", b"1" if words == 12 else b"2")
    address_action(p, b"Enter: continue")
    address_action(p, b"Enter/1-2: Select", b"2" if camera else b"1")
    address_action(p, b"Enter/1-3: Select", manual)


def copy_backup(p):
    while True:
        p.wait(b"Copy every word in order.")
        last = p.all_text.rsplit(b"\x1b[2J", 1)[-1]
        p.send(b"\r")
        if b"Enter: Check backup" in last:
            break
    found = re.findall(rb"\x1b\[\d+;\d+H(\d+)\. ([a-z]+)", p.all_text)
    return [value.decode() for _, value in sorted({int(index): value for index, value in found}.items())]


for rows, columns in ((12, 40), (24, 80)):
    p = Probe("entropy-generate", rows=rows, columns=columns)
    address_action(p, b"Enter/1-2: Select", b"\x1b[B")
    p.enter(b"> 2: 24 words", b"d\r")
    address_action(p, b"Enter: return to choices", b"\r\r")
    p.wait(b"> 2: 24 words")
    assert b"How your recovery words are created" not in p.all_text, "Details or queued Enter selected a word count"
    assert b"128 bits" in p.all_text and b"256 bits" in p.all_text
    p.enter(b"Esc: Back", b"\x1b")
    p.finish(2)

for count, rows, columns in ((12, 12, 40), (24, 24, 80)):
    p = Probe("entropy-generate", rows=rows, columns=columns)
    start_generation(p, count)
    rendered = b" ".join(re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b" ", p.all_text).split())
    assert b"phrase even stronger" in rendered
    assert b"Bitcoin Core's" in rendered and b"random-number generator" in rendered
    assert b"noise from the sensor" in rendered and b"This step is optional." in rendered
    assert (b"150 tosses" if count == 12 else b"300 tosses") in rendered
    # The last option may be below the compact menu's viewport until selected.
    if columns == 80:
        assert b"128 rolls" in rendered
    assert b"Write down your recovery words" not in p.all_text
    address_action(p, b"Enter: generate words")
    backup = copy_backup(p)
    assert len(backup) == count
    address_action(p, b"Enter: enter your words")
    if count == 12:
        for index, value in enumerate(MNEMONIC, 1):
            word(p, index, count, value)
        p.wait(b"Backup did not match")
        assert p.process.poll() is None, "Incorrect backup made the wallet available"
        address_action(p, b"Enter: review the words")
        assert copy_backup(p) == backup, "Retry generated different words"
        address_action(p, b"Enter: enter your words")
    for index, value in enumerate(backup, 1):
        word(p, index, count, value)
    output, _ = p.finish(0)
    assert b"BACKUP VERIFIED" in output

p = Probe("entropy-generate")
start_generation(p)
address_action(p, b"Enter: generate words", b"\x1b")
output, _ = p.finish(2)
assert b"Write down your recovery words" not in p.all_text and not output
p = Probe("entropy-rng-fail")
start_generation(p)
address_action(p, b"Enter: generate words")
output, _ = p.process.communicate(timeout=5)
assert p.process.returncode == 91 and not output
assert b"Write down your recovery words" not in p.all_text, "RNG failure exposed a fallback mnemonic"
print("PASS: manual outcomes, corrections, private backup verification, cancellation and fatal RNG failure")

for scenario, rows, columns in (("valid", 12, 40), ("valid", 40, 80), ("constant", 40, 80), ("frozen", 40, 80),
                               ("bad-header", 40, 80), ("stalled-capture", 40, 80), ("cancel-assessment", 40, 80), ("timeout-assessment", 40, 80)):
    # Build-time hardlinks keep fixtures executable on the read-only test image;
    # the writable /tmp mount deliberately remains noexec.
    probe = Path(sys.argv[1]).resolve().parent / "entropy-fixtures" / scenario / "terminal-probe"
    p = Probe("entropy-generate", executable=str(probe), rows=rows, columns=columns)
    start_generation(p, camera=True, manual=b"3" if scenario == "valid" else b"1")
    address_action(p, b"Enter: start")
    children = Path(f"/proc/{p.process.pid}/task/{p.process.pid}/children")
    if scenario == "cancel-assessment":
        p.wait(b"Camera: finished")
        pids = children.read_text().split()
        assert pids
        p.send(b"\x1b")
        p.finish(2)
        assert all(not Path(f"/proc/{pid}").exists() for pid in pids), "Cancelled assessment left a worker alive"
        continue
    if scenario == "valid":
        p.wait(b"d: Details", timeout=130)
        assert not children.read_text().strip(), "Workers survived into camera review"
        assert b"Estimated randomness:" not in p.all_text, "Camera summary exposed technical estimates"
        p.send(b"d")
        p.wait(b"d: Summary")
        address_action(p, b"Enter: use camera data")
        rendered = b" ".join(re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b" ", p.all_text).split())
        # Compact pages insert navigation text between wrapped lines.
        assert b"Estimated randomness:" in rendered and b"per 8-bit camera value" in rendered, rendered
        assert b"Camera values checked for randomness:" in rendered and b"1048576" in rendered
        assert b"tested camera data" in rendered and b"strength of your recovery words" in rendered
        assert b"Samples hashed" not in rendered and b"Hashed / assessed" not in rendered
        address_action(p, b"Enter: start collecting")
        p.wait(b"Rolls entered:")
        assert not children.read_text().strip(), "Manual input began with a worker alive"
        p.send(b"1" * 64)
        p.enter(b"Enter: Continue", b"\r")
        address_action(p, b"Enter: generate words")
        backup = copy_backup(p)
        address_action(p, b"Enter: enter your words")
        for index, value in enumerate(backup, 1):
            word(p, index, 12, value)
        output, _ = p.finish(0)
        assert b"BACKUP VERIFIED" in output
    else:
        p.wait(b"Camera not included", timeout=135)
        assert not children.read_text().strip(), "Failed camera attempt left a worker alive"
        address_action(p, b"Enter/1-2: Select", b"1")
        p.wait(b"Camera readings: not included")
        address_action(p, b"Enter: generate words", b"\x1b")
        p.finish(2)
        if scenario == "timeout-assessment":
            assert b"Camera collection and checks took too long" in p.all_text
    assert b"sha256" not in p.all_text and b"commandline" not in p.all_text, "Estimator metadata leaked onto the terminal"
print("PASS: complete camera/NIST/manual generation, malformed streams, constant samples, stalls, cancellation and total timeout")


# Exercise the actual application with a controlling tty. The test container has
# no framebuffer; a failed display must return to the menu and preserve the seed
# session, rather than prompt again or export through an alternate channel.
for choice, network, coin in [(b"\r", b"Bitcoin mainnet", 0), (b"2", b"Signet", 1)]:
    p = Probe(executable=sys.argv[2], controlling=True)
    p.enter(b"5: Legacy testnet3", choice)
    p.wait(network)
    p.enter(b"3: End session (clear keys)", b"2")
    p.enter(b"Enter a custom path (advanced)", b"8")
    p.enter(b"Path: ", b"\x1b")
    p.enter(b"3: End session (clear keys)", b"3")
    p.finish(0)

p = Probe(executable=sys.argv[2], controlling=True, rows=24)
p.wait(b"> 1: Bitcoin mainnet")
p.wait(b"2: Signet")
p.wait(b"3: Testnet4")
p.wait(b"5: Legacy testnet3")
for key in (b"\x1b", b"\x03"):
    for _ in range(5):
        p.send(key)
        time.sleep(0.02)
    assert p.process.poll() is None, "Cancel key ended network selection"
p.send(b"4")
for attempt in range(2):
    p.wait(b"3: End session (clear keys)")
    public_key_export(p)
    if attempt == 0:
        mnemonic(p, ["abandon"] * 12)
        p.wait(b"These words do not make a valid recovery phrase.")
        assert b"Passphrase: " not in p.all_text, "Passphrase requested before mnemonic validation"
        for index, value in enumerate(MNEMONIC, 1):
            word(p, index, 12, value)
        p.enter(b"Passphrase: ", b"\r")
    export_review(p)
    p.spaced(b"Press Enter to show the QR code.")
    assert b"Repeat passphrase: " not in p.all_text, "Empty passphrase required confirmation"
    p.send(b"\r")
    p.enter(b"framebuffer display is required", b"n")
p.wait(b"3: End session (clear keys)")
for key in (b"\x1b", b"\x03"):
    p.send(b"2")
    p.wait(b"Esc: Back")
    p.send(key)
    p.wait(b"3: End session (clear keys)")
    # Autorepeat continues after the next screen has flushed queued input.
    for _ in range(5):
        p.send(key)
        time.sleep(0.02)
    assert p.process.poll() is None, "Repeated cancellation ended the loaded session"
public_key_export(p)
export_review(p)  # Same keys, without re-entering the phrase.
p.send(b"q")
p.wait(b"3: End session (clear keys)")
p.send(b"2")
p.enter(b"Enter a custom path (advanced)", b"8")
p.enter(b"Path: ", b"m/84h/1h/0h\r")
p.enter(b"HD key QR (hdkey)", b"1")
export_review(p)
assert b"Path: m/84h/1h/0h" in p.all_text, "Public-key path was changed"
p.send(b"q")
p.wait(b"3: End session (clear keys)")
for choice, path, account_type in [(b"1", b"m/84h/1h/7h", b"Native SegWit (P2WPKH)"),
                                   (b"2", b"m/86h/1h/7h", b"Taproot (P2TR)"),
                                   (b"3", b"m/49h/1h/7h", b"Nested SegWit (P2SH-P2WPKH)"),
                                   (b"4", b"m/44h/1h/7h", b"Legacy (P2PKH)"),
                                   (b"5", b"m/48h/1h/7h/2h", b"Native SegWit multisig (P2WSH)"),
                                   (b"6", b"m/48h/1h/7h/1h", b"Nested SegWit multisig (P2SH-P2WSH)"),
                                   (b"7", b"m/45h", b"Legacy multisig (P2SH)")]:
    p.send(b"2")
    p.enter(b"Enter a custom path (advanced)", choice)
    if choice != b"7":
        p.enter(b"Account number 0-100 [0]: ", b"7\r")
    export_review(p)
    assert b"Address type: " + account_type in p.all_text
    assert b"Extended public key:" in p.all_text and b"QR format:" not in p.all_text
    p.send(b"q")
    assert b"Path: " + path in p.all_text
    p.wait(b"3: End session (clear keys)")
p.send(b"2")
p.enter(b"Enter a custom path (advanced)", b"8")
p.enter(b"Path: ", b"m/7h/3/9\r")
p.enter(b"HD key QR (hdkey)", b"2")
export_review(p)
p.send(b"q")
p.wait(b"3: End session (clear keys)")
p.send(b"3")
p.finish(0)
assert p.all_text.count(b"Your recovery phrase") == 1
assert b"abandon abandon" not in p.all_text
print("PASS: checksum checked before passphrase, single Enter for empty passphrase and one key session")
print("PASS: repeated Esc/Ctrl-C cancel operations without ending the session; explicit logout still works")

# All standard lengths are hidden by default, with the same checksum checks.
for choice, count, last in [(1, 12, "about"), (2, 15, "address"), (3, 18, "agent"),
                            (4, 21, "admit"), (5, 24, "art")]:
    words = ["abandon"] * (count - 1) + [last]
    p = Probe("mnemonic")
    mnemonic(p, words, str(choice).encode())
    p.finish_secret(" ".join(words).encode())
    assert b"abandon" not in p.all_text, "Recovery words were revealed by default"

# A failed checksum discards all words and retains the chosen length.
p = Probe("mnemonic")
mnemonic(p, ["abandon"] * 24, b"5")
p.wait(b"These words do not make a valid recovery phrase.")
words = ["abandon"] * 23 + ["art"]
for index, value in enumerate(words, 1):
    word(p, index, 24, value)
p.finish_secret(" ".join(words).encode())
assert p.all_text.count(b"Your recovery phrase") == 1

p = Probe("mnemonic")
mnemonic(p, ["abandon"] * 12)
p.wait(b"These words do not make a valid recovery phrase.")
p.wait(b"Word 1 of 12: ")
p.send(b"\x1b")
output, _ = p.finish(2)
assert not output
print("PASS: invalid phrases restart at word one with the same length and allow cancellation")

p = Probe("mnemonic")
p.enter(b"1: 12 words", b"\r")
word(p, 1, 12, "zzzz")
p.wait(b"\x1b[0;31;40mInvalid word.")
word(p, 1, 12, "abstractx")  # A valid eight-letter prefix must not be accepted.
p.wait(b"Invalid word.")
word(p, 1, 12, "ability")
p.wait(b"Word 2 of 12: ")
p.send(b"\r\x7f\t")  # Empty Enter/Backspace stay here; Tab still responds.
p.wait(b"You can see the word you are entering.")
p.wait(b"Word 2 of 12: ")
p.send(b"\x1b[A")
p.wait(b"Word 1 of 12: ")
p.wait(b"ability")
p.send(b"\x7f" * 7 + b"notaword\r")
p.wait(b"Invalid word.")
p.wait(b"Word 1 of 12: ")
p.send(b"abandon\r")  # No backspacing: the rejected replacement must be empty.
p.wait(b"Word 2 of 12: ")
assert b"You can see the word you are entering." in p.all_text.rsplit(b"\x1b[2J", 1)[-1], "Word visibility did not persist"
p.send(b"\t")
p.wait(b"Your words are hidden as you type")
for index, value in enumerate(MNEMONIC[1:], 2):
    word(p, index, 12, value)
p.finish_secret(" ".join(MNEMONIC).encode())

p = Probe("mnemonic")
p.enter(b"1: 12 words", b"\r")
p.wait(b"Word 1 of 12: ")
p.send(b"\r\x1b[A\x7f\t")  # Word one cannot go back or accept an empty value.
p.wait(b"You can see the word you are entering.")
p.wait(b"Word 1 of 12: ")
p.send(b"abandon\r")
p.wait(b"Word 2 of 12: ")
p.send(b"aban\x1b[A")  # Preserve this partial draft while revisiting word one.
p.wait(b"Word 1 of 12: ")
p.send(b"\r")
p.wait(b"Word 2 of 12: ")
p.wait(b"aban")
p.send(b"don\r")
for index, value in enumerate(MNEMONIC[2:], 3):
    word(p, index, 12, value)
p.finish_secret(" ".join(MNEMONIC).encode())

p = Probe("mnemonic")
p.enter(b"1: 12 words", b"\r")
word(p, 1, 12, "abandon")
p.wait(b"Word 2 of 12: ")
p.send(b"abstractx\x1b[A")  # Do not save a truncated valid prefix as a draft.
p.wait(b"Invalid word.")
p.wait(b"Word 2 of 12: ")
p.send(b"\x1b[A")
p.wait(b"Word 1 of 12: ")
p.send(b"\r")
for index, value in enumerate(MNEMONIC[1:], 2):
    word(p, index, 12, value)
p.finish_secret(" ".join(MNEMONIC).encode())

p = Probe("mnemonic")
p.enter(b"1: 12 words", b"\x1b")
output, _ = p.finish(2)
assert not output

p = Probe("mnemonic")
p.enter(b"1: 12 words", b"\r")
word(p, 1, 12, "abandon")
p.wait(b"Word 2 of 12: ")
p.send(b"\x1b")
output, _ = p.finish(2)
assert not output
print("PASS: hidden recovery words, opt-in visibility, rejected replacements, overlong words and cancellation")

# A mistyped passphrase can be retried without entering the words again.
p = Probe(executable=sys.argv[2], controlling=True, rows=24)
p.enter(b"5: Legacy testnet3", b"4")
p.wait(b"3: End session (clear keys)")
public_key_export(p)
mnemonic(p, MNEMONIC)
p.enter(b"Passphrase: ", b" A Case  \r")
p.enter(b"Repeat passphrase: ", b" A Case\r")
p.wait(b"Press Enter to continue.")
assert b"Passphrases did not match" in p.all_text
assert b"Page 1/1" not in p.all_text, "Single-page notices have a pager"
p.send(b"\x1b")
p.wait(b"Passphrase: ")
p.spaced(b"Passphrase:")
# Holding Escape after dismissing the error must not cancel the renewed entry.
time.sleep(0.55)
for _ in range(8):
    p.send(b"\x1b")
    time.sleep(0.04)
p.send(b" A Case  ")
p.send(b"\t")
p.wait(b"Your passphrase is visible as you type.")
p.wait(b" A Case  ")
p.send(b"\r")
p.wait(b"Repeat your passphrase")
p.wait(b"Your passphrase is hidden as you type")
p.wait(b"Repeat passphrase: ")
p.spaced(b"Repeat passphrase:")
confirmation_start = len(p.all_text)
p.send(b" A Case  \r")
export_review(p)
p.send(b"q")
p.wait(b"3: End session (clear keys)")
p.send(b"3")
p.finish(0)
assert p.all_text.count(b"Your recovery phrase") == 1
assert b" A Case" not in p.all_text[confirmation_start:], "Confirmation inherited the visible state"
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
    for words, passphrase, network in [(MNEMONIC, b"", b"4"), (["all"] * 12, b"TREZOR", b"3")]:
        p.wait(b"5: Legacy testnet3")
        pid, = children.read_text().split()
        assert pid not in pids, "Session reused the previous signer process"
        pids.append(pid)
        p.send(network)
        p.wait(b"Regtest" if network == b"4" else b"Testnet4")
        p.wait(b"3: End session (clear keys)")
        public_key_export(p)
        mnemonic(p, words)
        p.enter(b"Passphrase: ", passphrase + b"\r")
        if passphrase:
            p.enter(b"Repeat passphrase: ", passphrase + b"\r")
        export_review(p)
        fingerprints.append(re.findall(rb"Master fingerprint: ([0-9a-f]{8})", p.all_text)[-1])
        p.send(b"q")
        p.wait(b"3: End session (clear keys)")
        p.send(b"3")
        p.wait(b"Session ended")
        p.wait(b"Thunder Den has cleared the recovery words")
        p.wait(b"Press Enter to start a new session.")
        p.spaced(b"Press Enter to start a new session.")
        assert p.rows()[2] == b"THUNDER DEN", "Session-ended layout differs from the app"
        assert not Path(f"/proc/{pid}").exists(), "Old signer is still alive"
        viewer, = children.read_text().split()
        assert viewer != pid and Path(f"/proc/{viewer}/cmdline").read_bytes().endswith(b"--session-ended\0"), "Completion is not a fresh keyless viewer"
        p.send(b"x\x1b")
        time.sleep(0.05)
        assert children.read_text().split() == [viewer], "Session restarted without Enter"
        if len(pids) == 1:
            p.send(b"\r")
    assert fingerprints[0] != fingerprints[1], "New seed reused the old wallet"
    assert p.all_text.count(b"Your recovery phrase") == 2
    assert b"TREZOR" not in p.all_text
    p.send(b"\x04")  # EOF exits the viewer without restarting the signer.
    p.wait(b"Signer stopped.")
    assert not children.read_text().strip(), "End of input restarted the signer"
    p.process.terminate()
    p.finish(-signal.SIGTERM)

    launcher.write_text(script.replace("/usr/bin/thunderden-signer", "/bin/false"))
    p = Probe(str(launcher), executable="/bin/sh", controlling=True, tty_output=True)
    p.wait(b"Signer stopped.")
    assert b"Thunder Den has cleared" not in p.all_text, "Failed signer reported successful cleanup"
    p.process.terminate()
    p.finish(-signal.SIGTERM)
print("PASS: logout waits for process exit, stays idle until Enter and starts fresh keys, passphrase and network")
