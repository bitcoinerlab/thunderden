# Scanner isolation

Thunder Den receives proposed transactions and wallet definitions through QR codes
displayed by a software wallet. Recovery words and private keys stay on the signing
laptop, where you review and approve each transaction.

**The goal of scanner isolation is to keep a flaw in QR-reading software from
giving an attacker access to those keys or control over approval.** Thunder Den
runs the scanner separately from the program that holds the keys, with Linux
enforcing restrictions on what the scanner can access.

## Why this protection exists

Incoming requests are untrusted. Thunder Den checks their structure, wallet rules
and transaction details, then requires local review and confirmation. A malicious
software wallet can propose an unwanted payment; checking the destination, amount
and fee on the laptop lets you reject it.

Scanner isolation adds protection against a more complex attack: exploiting the
software that reads the QR itself. An attacker would need to:

1. Control an image or QR message that you scan, for example by compromising the
   software wallet or the device displaying it.
2. Craft input that exploits a suitable flaw in image, QR or multipart-message
   decoding and turns that flaw into control over the scanner.
3. Get past the scanner's operating-system restrictions or exploit the trusted
   signing software to reach keys or affect approval.

This is a demanding attack chain. The decoder flaw could be undisclosed or known
but still present in the installed version. Isolation is **defense in depth**:
an extra barrier against a decoder compromise, alongside input validation and
local approval.

## Two programs with separate responsibilities

The application uses two Linux processes: running programs with separate memory.

- **Scanner (`thunderden-scanner`):** opens the camera, converts its images, reads
  QR codes and assembles animated QR messages. It receives no recovery words or
  private keys.
- **Signer (`thunderden-signer`):** holds the keys, validates wallet definitions
  and transactions, renders the review, accepts keyboard approval and signs.

```text
camera -> scanner -> limited data channel -> signer -> local review and approval
          no keys                           keys       -> signed response QR
```

Each scan starts a fresh scanner executable, without a copy of the signer's key
memory or access to its keyboard and screen connections. The scanner sends only
camera previews and completed request data through a pipe: a one-way channel
between the programs. The signer checks each record's type and size before
allocating memory, then validates the request through its normal wallet and
transaction checks. Scanner output always remains untrusted.

This channel has no way to approve a transaction. Camera previews appear only
during scanning. Before review begins, the signer terminates the scanner and waits
for it to exit; the signer then draws the review itself. Signing requires traversing
the review pages and typing `SIGN` on the laptop. A stalled or partially written
scanner response still allows keyboard cancellation.

## How Linux enforces the separation

Before accepting recovery words, the application prepares camera and display
permissions and gives up administrator access. Both programs run under an ordinary
user identity, with extra privileges removed and process-memory dumps disabled.
Installed programs and their directories are owned by the administrator and cannot
be rewritten by this identity.

The scanner also uses **Landlock**, a Linux facility that lets a process permanently
restrict its own access. It opens the camera and initializes the decoder, then
applies these restrictions **before reading camera frames**:

- New access to file contents, filesystem changes and execution of filesystem
  programs are denied. Already-open camera and pipe connections remain usable.
- The scanner cannot inspect the signer's memory or send it process-control signals.
- Memory, CPU work and open files/devices are bounded; additional processes and
  threads are forbidden.

The image's Linux kernel has networking disabled entirely. Landlock enforcement
uses the kernel API directly. If the required protection is unavailable, the
scanner refuses to scan.

Only the scanner loads ZBar, libv4l and JPEG decoding libraries. Its executable
contains no key-session, signing, keyboard or screen-rendering implementation.
The image includes only the required BusyBox startup/shutdown helpers, and image
checks reject executables that grant extra user or group privileges.

## Scope and practical limits

The signer still interprets decoded wallet and transaction data using Bitcoin
Core. Its parsers, the review/signing logic and the code reading scanner records
remain trusted. Linux must enforce the boundary correctly; hardware and firmware
also remain trust dependencies. Isolation limits the consequences of scanner
compromise, without establishing that all native-code or kernel bugs are impossible.

Camera/display permissions are assigned after initial Bitcoin network selection.
A camera connected or recreated afterwards may require restarting the image.
Physical webcam streaming, reconnects and slow-camera behavior remain hardware
validation work; current coverage is listed in [Implementation status](STATUS.md).

## Implementation and verification reference

- [Scanner startup](../src/scanner_main.cpp) applies confinement before capture.
  [Privilege restrictions](../src/isolation.cpp) use uid/gid 1000, cleared
  supplementary groups and capabilities, `no_new_privs` and disabled process dumps.
  Landlock ABI 6 or newer is required and enabled in the pinned image kernel.
- [Process lifecycle and pipe](../src/scan.cpp) use a fixed executable location,
  a fresh session, a minimal environment and closed inherited device descriptors.
  Records have five little-endian uint32 fields: kind, width, height, progress and
  payload size. Previews are limited to 1920×1080 grayscale pixels and progress
  0–100; completed messages follow the [QR protocol limits](PROTOCOL.md).
  Nonblocking reads use a maximum 50 ms poll interval.
- Per-scanner resource limits are 256 MiB of address space, 60 CPU seconds,
  64 file descriptors and no additional child processes or threads.
- [Isolation tests](../tests/isolation.cpp) check fresh process state, restricted
  privileges, denied application-file writes, real JPEG/QR/UR decoding under
  confinement, malformed and partial pipe records, cancellation, process cleanup,
  memory limits and denied filesystem and parent-process access. JPEG conversion
  uses the real conversion libraries with mocked camera-driver controls.
- [Build and tests](BUILD.md) describes the default native suites and optional
  checks on the image's own kernel and libraries. Host containment tests report
  a skip when Landlock ABI 6 is unavailable. Guest checks use a separate test-only
  overlay; development tools and test executables are kept outside the installed
  signer image.
