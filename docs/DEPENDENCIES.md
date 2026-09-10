# Dependencies

Authoritative source versions and hashes are in the repository's `.env` file.
The build uses Bitcoin Core 31.1 and a digest/snapshot-pinned Debian 13.6 build
environment. Buildroot supplies the image toolchain, runtime libraries, Linux,
BusyBox, and GRUB.

## Runtime components

| Component | Purpose |
| --- | --- |
| Bitcoin Core internal libraries | BIP32, keys, descriptors/Miniscript, PSBTs, signing/script verification, hashes, and serialization |
| Core's bundled libsecp256k1 | Elliptic-curve operations |
| OpenSSL libcrypto | BIP39 PBKDF2 and constant-time comparison |
| BIP39 English wordlist | Standard recovery-word data |
| C/C++ runtime | Allocation, standard containers, and operating-system interfaces |
| bc-ur reference implementation | UR v2 Bytewords and fountain encoding/decoding |
| ZBar 0.23.93 | QR recognition in grayscale camera images |
| libqrencode 4.1.1 | QR module generation |
| libv4l 1.28.1 and JPEG library | Webcam capture and common image-format conversion |

Core is built from unmodified source. Its internal archives are build artifacts,
not a shared wallet SDK. Linker garbage collection removes unused code. The native
signing test executable links libcrypto and the standard C/C++ runtime libraries;
selected node, RPC, wallet, and LevelDB symbols are absent. The node/wallet/LevelDB
archives are not built.

Core's shared descriptor/signing implementation retains MuSig helpers even though
Thunder Den rejects MuSig policies and PSBT input metadata. Core RNG, secure
allocation, logging, and filesystem-support code also remain dependencies. Runtime
checks of the signing tests observe system-metadata reads and local NETLINK_ROUTE
queries, no IP sockets or filesystem-write opens, and successful signing with socket
creation unavailable. These checks cover the exercised library paths. A complete
packed-initramfs inventory is emitted as `installed-files.json`. The native runtime
check inspects the development signer/scanner symbols and shared-library dependencies
and reports their stripped executable sizes.

## QR and camera configuration

ZBar and libqrencode use C APIs that accept/produce image data directly and require
no desktop framework. ZBar is built with QR symbology only, without its video
frontend, JPEG converter, GUI bindings, DBus, or language bindings. libqrencode is
built without its tools or PNG support.
The checked development builds of these two libraries depend only on libc.

libv4l handles webcam negotiation and conversion, including normal MJPEG cameras.
Its optional plugins, wrappers, and utility programs are disabled. The checked
capture-library closure consists of libv4l2, libv4lconvert, libjpeg, libc, and libm.
The image targets standard UVC webcams, including typical integrated laptop cameras.
Physical camera compatibility still requires hardware tests.

ZBar, libv4l, and JPEG decoding run in `thunderden-scanner`; these libraries are
absent from the signing executable's dependencies. Scanner confinement uses the
pinned Linux kernel's Landlock API directly. See [Scanner isolation](ISOLATION.md)
for the threat model, boundary, and compatibility requirements.

bc-ur is pinned by commit and archive hash. The adapter validates URI fields,
unsigned CBOR lengths, fragment geometry, stream identity, and work limits before
passing a constructed part to its fountain decoder. Upstream's generic CBOR byte
decoder is not used for scanned lengths. Two missing transitive standard-header
includes are supplied by compiler options; the source is unmodified. Its bundled
CRC/SHA and deterministic fountain-mixing code processes public transport data.
Bitcoin key derivation and signing remain Core operations. BBQR is not implemented.

## Development

Docker supplies the compiler, CMake, Python test runner, binary inspection tools,
and source downloads. These tools are not installed in the signer image.
The independent `urtypes` 1.0.1 registry codec is hash-pinned for export-compatibility
tests only. Debian's development libcrypto also links zlib/zstd; this does not add
BBQR support. The image builds its own dependency configuration through Buildroot.

## Trust boundary

The project audit covers Thunder Den's own code, upstream configuration, installed
contents, and integration. It does not replace upstream audits of Linux, Core,
OpenSSL, the toolchain, or hardware/firmware. Pinning identifies exactly which
upstream code is trusted; reproducibility ties source builds to published bytes.
