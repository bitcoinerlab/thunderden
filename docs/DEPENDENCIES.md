# Dependencies

Authoritative source versions and hashes are in the repository's `.env` file.
The build uses Bitcoin Core 31.1 and a digest/snapshot-pinned Debian 13.6 toolchain
environment. Linux and Buildroot pins are reserved for boot-image integration.

## Native library

| Component | Purpose |
| --- | --- |
| Bitcoin Core internal libraries | BIP32, keys, descriptors/Miniscript, PSBTs, signing/script verification, hashes, and serialization |
| Core's bundled libsecp256k1 | Elliptic-curve operations |
| OpenSSL libcrypto | BIP39 PBKDF2 and constant-time comparison |
| BIP39 English wordlist | Standard recovery-word data |
| C/C++ runtime | Allocation, standard containers, and operating-system interfaces |

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
installed-file and runtime dependency inventory will accompany the bootable image.

## Development

Docker supplies the compiler, CMake, Python test runner, binary inspection tools,
and source downloads. These tools are not installed in the signer image.

## Trust boundary

The project audit covers Thunder Den's own code, upstream configuration, installed
contents, and integration. It does not replace upstream audits of Linux, Core,
OpenSSL, the toolchain, or hardware/firmware. Pinning identifies exactly which
upstream code is trusted; reproducibility ties source builds to published bytes.
