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

## Signer and scanning workers with separate responsibilities

The signer and its active scanning worker use separate Linux processes and memory.

- **Scanner (`thunderden-scanner`):** opens the camera, converts its images, reads
  QR codes and assembles animated QR messages. It receives no recovery words or
  private keys.
- **Signer (`thunderden-signer`):** holds the keys, validates wallet definitions
  and transactions, renders the review, accepts keyboard approval and signs.
- **Experimental OCR worker (`thunderden-ocr`):** on `feature/seed-ocr`, reads a
  paper recovery phrase before key derivation. Unlike the QR worker, it necessarily
  sees the photographed words. It receives no signer memory, passphrase or private
  keys, and returns only a bounded proposal for local review. It exits before
  editing begins. The [OCR guide](OCR.md) documents this additional input boundary.
  CTC vocabulary ranking also runs inside this worker. The signer accepts at most
  five distinct, validated BIP39 candidates per token; confidence flags are
  untrusted suggestions, never authorization. Attached numeric prefixes also carry
  a prefix width and word offset; the signer validates their bounds and consistency
  with the raw prefix before using them as label-position evidence. Missing or
  unreadable slots require local correction before the phrase can pass validation.

```text
camera -> scanner -> limited data channel -> signer -> local review and approval
          no keys                           keys       -> signed response QR
```

Each scan starts a fresh scanner executable, without a copy of the signer's key
memory or access to its keyboard and screen connections. The scanner sends
camera previews, completed request data or fixed failure codes through an output
pipe. The signer checks each record's type and
size before allocating memory, then validates requests through its normal wallet
and transaction checks. Scanner output always remains untrusted.
The OCR worker additionally accepts one 16-byte crop rectangle through a separate
input pipe, connected in place of keyboard input. Both ends validate it against
the frozen frame. Frozen-frame messages are accepted only after capture, and
word results only after local area confirmation. This control channel carries
coordinates, not keys or approval.

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
- Memory and open files/devices are bounded; additional processes and threads
  are forbidden.

The signer stops a scan after ten seconds without a complete preview or result.
Partial pipe traffic does not renew this deadline. Healthy scanning has no total
CPU-time cutoff. This is a liveness check, not a CPU budget: a compromised scanner
could keep it satisfied by sending valid preview records. Local cancellation
still terminates the worker.

The image's Linux kernel has networking disabled entirely. Landlock enforcement
uses the kernel API directly. If the required protection is unavailable, the
scanner refuses to scan.

Image decoding libraries stay in the scanning workers, outside the signer.
The QR worker uses ZBar/libv4l/JPEG; the OCR worker uses libv4l/JPEG and a CPU-only
OpenCV subset. Neither executable contains key-session, signing, keyboard or
screen-rendering implementations. The OCR worker loads pinned local models before
confinement and uses the same 256 MiB address-space and filesystem/process limits.
OCR startup, frame preparation and selected-area recognition have separate
60-second deadlines, not renewed by preview traffic once capture is requested.
The local rectangle-selection step is untimed and cancellable. Capture is
requested by a signal; recognition starts after the crop message. Worker-to-parent
traffic cannot approve a seed.
The image includes only the required BusyBox startup/shutdown helpers, and image
checks reject executables that grant extra user or group privileges.

## Scope and practical limits

The signer still interprets decoded wallet and transaction data using Bitcoin
Core. Its parsers, the review/signing logic and the code reading scanner records
remain trusted. Linux must enforce the boundary correctly; hardware and firmware
also remain trust dependencies. Isolation limits the consequences of scanner
compromise, without establishing that all native-code or kernel bugs are impossible.

Camera/display permissions are assigned after initial Bitcoin network selection.
A camera connected or recreated afterwards may require a new session to refresh
those permissions.
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
  Failure records have kind 4, a fixed failure code (1–7) in the progress field
  and zero width, height and payload size. The signer maps these codes to its own
  messages; decoder-provided text is never displayed.
  Nonblocking reads use a maximum 50 ms poll interval.
- Per-scanner resource limits are 256 MiB of address space, 64 file descriptors
  and no additional child processes or threads. A parent-side ten-second
  no-frame deadline covers startup and stalled capture/decoding.
- [Isolation tests](../tests/isolation.cpp) check fresh process state, restricted
  privileges, denied application-file writes, real JPEG/QR/UR decoding under
  confinement, malformed and partial pipe records, cancellation, process cleanup,
  memory limits, no-frame timeouts, fixed failure reports and denied filesystem
  and parent-process access. JPEG conversion uses the real conversion libraries
  with mocked camera-driver controls.
- [Build and tests](BUILD.md) describes the default native suites and optional
  checks on the image's own kernel and libraries. Host containment tests report
  a skip when Landlock ABI 6 is unavailable. Guest checks use a separate test-only
  overlay; development tools and test executables are kept outside the installed
  signer image.
