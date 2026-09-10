# Build and tests

Requires Docker with Compose and Linux-container support.

```text
docker compose run --build --rm test
```

The development build compiles the native Core-backed libraries and runs their
tests. It does not yet produce a bootable v2 image. Current implementation and
verification coverage are listed in [STATUS.md](STATUS.md).

Source versions, hashes, the base-image digest, and Debian snapshot are defined in
`.env`. Build products remain inside Docker. A CMake build can also use an existing
verified Core source tree by supplying `CORE_SOURCE_DIR` and `BIP39_WORDLIST`.
The `BIP39_WORDLIST_SHA256` setting must match the pinned wordlist hash.

The four CTest suites cover seed/policy handling, transaction review/signing, Core
key regression vectors, and native dependency/syscall checks. Signing tests use
public deterministic fixtures and synthetic previous transactions. Linker wrappers
count ECDSA/Schnorr calls to check that review and rejection do not sign.

To see individual checks, dependency details, and test-executable size measurements
after building:

```text
docker compose run --rm test ctest --test-dir /build --output-on-failure -V
```

All current executable targets are development-only. Reported test-executable
sizes include fixtures and are not production signer or image sizes. Production
installation and image assembly remain pending.
