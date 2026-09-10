# QR protocol

The application uses UR v2 with Bytewords-minimal encoding and fountain-coded
multipart messages. PSBTs remain version 0. BBQR and legacy multipart formats are
not supported. Outgoing UR text is uppercase for QR alphanumeric encoding.

## Standard PSBT exchange

`ur:crypto-psbt` contains a CBOR byte string holding the raw PSBT. For an incoming
plain PSBT, the user selects a BIP44/49/84/86 account locally. The application builds
the exact default policy from the seed and uses the normal policy/approval engine.
The unsigned request is never allowed to choose a private-key path implicitly.
The result is another `ur:crypto-psbt`, including when only partially signed.

Account export uses `ur:crypto-account` with a public HD key, origin, network,
and script expression. It requires local approval before display.

## Wallet-policy operations

Custom policy messages use the deployed legacy `ur:bytes` convention: a CBOR byte
string containing UTF-8 JSON. These commands are Thunder Den-specific; existing
wallets need an integration to send the policy and store its proof. Transport
support alone does not implement registration.

JSON objects have exactly the documented fields. Duplicate or unknown keys,
unsupported versions, and mismatched networks are rejected. Wallet strings are
passed unchanged to policy validation; JSON whitespace is not part of the wallet ID.

Registration request:

```json
{
  "version": 1,
  "command": "REGISTER_WALLET",
  "network": "testnet4",
  "wallet": {
    "name": "Savings",
    "template": "wpkh(@0/**)",
    "keys": ["[fingerprint/origin]tpub..."]
  }
}
```

The key above is a placeholder. Real requests require complete valid key
information. Registration returns `ur:bytes` JSON after local approval:

```json
{
  "version": 1,
  "command": "WALLET_REGISTERED",
  "wallet_id": "64 lowercase hexadecimal characters",
  "wallet_hmac": "64 lowercase hexadecimal characters"
}
```

Signing request:

```json
{
  "version": 1,
  "command": "SIGN_PSBT",
  "network": "testnet4",
  "wallet": {
    "name": "Savings",
    "template": "wpkh(@0/**)",
    "keys": ["[fingerprint/origin]tpub..."]
  },
  "wallet_hmac": "64 hexadecimal characters",
  "psbt": "standard padded base64 PSBT"
}
```

The response is standard `ur:crypto-psbt`. The complete policy accompanies every
custom signing request. Registration approval and transaction approval are distinct.
Cancellation returns to the menu without displaying a response QR.

Network identifiers are `main`, `testnet`, `testnet4`, `signet`, and `regtest`.
Network selection is local and fixed for an application session.

## Bounds and assembly

- Individual QR text: at most 4,296 ASCII characters.
- Message: at most 2 MiB + 64 KiB of CBOR, with tighter application/PSBT limits.
- At most 1,024 source fragments; at most `4 * fragment_count + 64` distinct
  sequence numbers per scan. Identical repeated frames do not consume this budget.
- The type, fragment count/size, message length, and checksum must stay consistent.
- Conflicting duplicate frames are rejected. A new stream requires a new scan.
- Both individual Bytewords checksums and the completed fountain checksum must pass.
- Lengths and geometry are checked before upstream fountain allocation/processing.

The sender increases fragment size for large messages to stay within the fragment
count bound. Large PSBTs may therefore require denser QR codes. The receiver checks
the CBOR byte-string length using unsigned bounds before iterator arithmetic; it
does not expose the upstream generic CBOR parser directly to scanned lengths.

`tests/transport.cpp` checks published UR vectors, mixed-frame recovery, malformed
lengths/counts, stream conflicts, and image encoding/decoding through libqrencode
and ZBar. These are development protocol tests, not physical webcam certification.
