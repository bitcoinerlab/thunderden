# Pinned Liana installation

Use this source/package combination for Thunder Den's regular desktop Liana
integration with Bitcoin Core or Electrum. Thunder Den support is fork-only;
Liana Connect is disabled. The [recorded verification scope](STATUS.md#liana-integration)
covers builds, native tests and synthetic interoperability. A complete physical
Liana signing flow remains outstanding.

## Version manifest

| Component | Pinned revision or release |
| --- | --- |
| Liana GUI | [`85e7681fc0aec8ab3fcfb11b47cb5b32d3cac63d`](https://github.com/bitcoinerlab/wizardsardine-liana/tree/85e7681fc0aec8ab3fcfb11b47cb5b32d3cac63d) |
| async-hwi adapter and CLI | [`dbbd1a7d29ddda504eb62b5f9d750d7c4ef21d34`](https://github.com/bitcoinerlab/wizardsardine-async-hwi/tree/dbbd1a7d29ddda504eb62b5f9d750d7c4ef21d34) |
| Published QR bridge | `@bitcoinerlab/thunderden-qr-bridge@0.1.2` |
| QR-bridge release source | [`59a6aa35cbfc4b97f0ea99d64fae62a33e49e68d`](https://github.com/bitcoinerlab/thunderden-qr-bridge/tree/59a6aa35cbfc4b97f0ea99d64fae62a33e49e68d) |
| Rust toolchain | `1.88.0`, also selected by Liana's checked-in `rust-toolchain.toml` |

Liana's [Cargo.toml](https://github.com/bitcoinerlab/wizardsardine-liana/blob/85e7681fc0aec8ab3fcfb11b47cb5b32d3cac63d/Cargo.toml)
and [Cargo.lock](https://github.com/bitcoinerlab/wizardsardine-liana/blob/85e7681fc0aec8ab3fcfb11b47cb5b32d3cac63d/Cargo.lock)
both select the adapter revision above. Keep the checked-in lockfiles and use
`--locked`; Cargo must fail rather than silently resolve a different dependency
set. These pins identify application sources and dependency resolution, not a
bit-for-bit reproducible build of the online computer's entire environment.

## Build and run the GUI

Install Rust `1.88.0` through rustup and the GUI's
[platform dependencies](https://github.com/bitcoinerlab/wizardsardine-liana/blob/85e7681fc0aec8ab3fcfb11b47cb5b32d3cac63d/liana-gui/README.md#dependencies).
From a directory where you want a new checkout:

```sh
git clone https://github.com/bitcoinerlab/wizardsardine-liana.git liana-thunderden
git -C liana-thunderden checkout --detach 85e7681fc0aec8ab3fcfb11b47cb5b32d3cac63d
cargo +1.88.0 build --locked --release --manifest-path liana-thunderden/Cargo.toml --package liana-gui
./liana-thunderden/target/release/liana-gui
```

The executable is `liana-gui.exe` on Windows. Cargo builds the pinned async-hwi
dependency automatically; the GUI does not require a separate adapter checkout.
Choose Bitcoin Core or Electrum and the same Bitcoin network as Thunder Den.

## Run the published bridge

On the online computer running Liana, use Node.js 22 or later:

```sh
npx @bitcoinerlab/thunderden-qr-bridge@0.1.2
```

Keep the bridge and its browser page open, then select Thunder Den in Liana's
hardware-wallet list. The npm release contains its built browser frontend and
has no runtime npm dependencies. Later bridge source commits are not part of
the pinned `0.1.2` package.

### Verify the published archive

The release's [npm metadata](https://registry.npmjs.org/@bitcoinerlab%2fthunderden-qr-bridge/0.1.2)
identifies the source commit above. Its downloaded tarball has SHA-256:

```text
049086935da442cc8094ead4dc2eea1ae1477e881339a55ee239cbdaf1329285
```

To check the archive before running its contents, use a fresh temporary directory:

```sh
npm pack --ignore-scripts @bitcoinerlab/thunderden-qr-bridge@0.1.2
printf '%s  %s\n' 049086935da442cc8094ead4dc2eea1ae1477e881339a55ee239cbdaf1329285 bitcoinerlab-thunderden-qr-bridge-0.1.2.tgz | sha256sum -c - &&
tar -xzf bitcoinerlab-thunderden-qr-bridge-0.1.2.tgz &&
node package/bridge.js
```

On macOS, replace `sha256sum -c -` with `shasum -a 256 -c -`.
The `&&` chain runs the archive only after its checksum succeeds. No `npm install`
or frontend build is needed for this published archive. To rebuild from release
source instead, use its checked-in `package-lock.json` with `npm ci --ignore-scripts`
before `npm run build`.

## CLI for the walkthrough

The [command-line walkthrough](WALKTHROUGH.md) uses the same async-hwi revision.
With Rust `1.88.0` installed (plus `pkg-config` and `libudev-dev` on Debian/Ubuntu):

```sh
git clone https://github.com/bitcoinerlab/wizardsardine-async-hwi.git async-hwi-thunderden
git -C async-hwi-thunderden checkout --detach dbbd1a7d29ddda504eb62b5f9d750d7c4ef21d34
cargo +1.88.0 build --locked --release --manifest-path async-hwi-thunderden/Cargo.toml --package async-hwi-cli
./async-hwi-thunderden/target/release/hwi --help
```

Use that `hwi` binary for the walkthrough. On Windows its filename is `hwi.exe`.

## Updating the pins

Update this manifest, installation commands and verification record together.
Check the new lockfiles and published archive, and retest the integration before
recommending a new combination. Preserve earlier verification as evidence for
the revisions it actually covered.
