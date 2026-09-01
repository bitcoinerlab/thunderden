# Wallet Policy Architecture

## Status

This document defines the target architecture after the current BIP84 MVP. It
is not implemented yet.

Thunder Den remains stateless. The coordinator stores each wallet policy and
its registration HMAC, then supplies both whenever it asks Thunder Den to sign.

## Standards

- PSBT version 0: BIP-174. BIP-370 PSBTv2 remains unsupported while Bitcoin Core cannot parse it.
- Wallet policies: BIP-388.
- Output descriptors: BIP-380 and related descriptor BIPs.
- Miniscript: BIP-379 when support is added.
- Symmetric key derivation: SLIP-0021.
- Wallet policy ID and default-wallet rules: Ledger Bitcoin app policy version 2.

The airgap request and response encoding is Thunder Den-specific. It must not
change the BIP-388 wallet policy or the Ledger-compatible wallet ID.

## Policy representation

A registration record contains:

- A wallet name committed as Ledger registration metadata.
- A BIP-388 wallet policy containing a descriptor template and an ordered key information vector.

Example:

```text
name: Family multisig
descriptor template: wsh(sortedmulti(2,@0/**,@1/**,@2/**))
keys:
  [f5acc2fd/48'/0'/0'/2']xpub...
  [12345678/48'/0'/0'/2']xpub...
  [87654321/48'/0'/0'/2']xpub...
```

The complete policy is supplied during registration and again during every
signing request. Thunder Den does not persist it.

## Wallet ID

Thunder Den uses the Ledger Bitcoin app version 2 wallet ID byte-for-byte.

```text
serialized_wallet_policy =
    0x02
    || uint8(wallet_name_length)
    || wallet_name
    || compact_size(descriptor_template_length)
    || SHA256(descriptor_template)
    || compact_size(number_of_keys)
    || merkle_root(keys)

wallet_id = SHA256(serialized_wallet_policy)
```

Lengths count bytes, not characters. Policy names are printable ASCII, so the
byte and character lengths are identical. Descriptor templates and key
information strings are hashed exactly as supplied by the policy.

The ordered key vector uses Ledger's RFC-6962-style Merkle tree:

```text
MTH([]) = 32 zero bytes
MTH([d]) = SHA256(0x00 || d)
MTH(D) = SHA256(0x01 || MTH(D[0:k]) || MTH(D[k:n]))
```

For the final rule, `n` is the number of entries and `k` is the largest power
of two strictly smaller than `n`.

Matching Ledger's wallet ID lets coordinators and test vectors use the same
stable account identifier. It does not make Thunder Den registration HMACs
valid on Ledger devices.

## Registration HMAC

Thunder Den uses the Ledger registration construction with a separate
SLIP-0021 application label:

```text
master_node = HMAC-SHA512(
    key = "Symmetric key seed",
    message = bip39_seed
)

policy_node = HMAC-SHA512(
    key = master_node[0:32],
    message = 0x00 || "Thunder Den wallet policy"
)

registration_key = policy_node[32:64]
wallet_hmac = HMAC-SHA256(registration_key, wallet_id)
```

The BIP39 passphrase is part of the BIP39 seed and therefore changes the HMAC.
The HMAC is deterministic, seed-bound, and non-revocable. It proves that the
same seed previously approved the exact named policy. It is not a replacement
for backing up a custom policy.

OpenSSL already provides SHA-256, HMAC-SHA256, and HMAC-SHA512. No additional
cryptographic library is required. Implementations must keep seed and HMAC key
material in RAM files and must include SLIP-0021 and wallet-policy known-answer
tests.

## Default wallets

Thunder Den follows the Ledger Bitcoin app default-wallet rules. These
single-key policies do not require registration:

| Standard | Descriptor template | Account origin |
| --- | --- | --- |
| BIP44 | `pkh(@0/**)` | `m/44'/coin_type'/account'` |
| BIP49 | `sh(wpkh(@0/**))` | `m/49'/coin_type'/account'` |
| BIP84 | `wpkh(@0/**)` | `m/84'/coin_type'/account'` |
| BIP86 | `tr(@0/**)` | `m/86'/coin_type'/account'` |

A default wallet must have:

- An empty wallet name.
- Exactly one key.
- The exact standard descriptor template and origin path.
- The correct coin type and extended-key network version.
- An account index from 0 through 100.
- An address index from 0 through 50000 when deriving or displaying a new address.
- An origin fingerprint matching the entered seed.
- An xpub exactly equal to the xpub re-derived from that seed and origin path.
- A 32-byte all-zero registration HMAC in the signing request.

A fingerprint match alone is never proof of ownership. Any non-standard path,
name, template, or key arrangement requires registration. Signing must still
allow a valid policy-owned input above index 50000 so previously received funds
cannot become unspendable.

## Registered wallets

Every policy that is not an exact default wallet requires registration. This
includes multisig, Miniscript, taproot trees, unusual paths, and named
single-signature accounts.

A registered name must contain 1 through 64 printable ASCII characters and
must not start or end with a space.

Registration must reject policies that:

- Have no key controlled by the entered seed.
- Claim ownership using only a matching fingerprint.
- Contain invalid, duplicate, unused, or network-incompatible keys.
- Use unsupported BIP-388 templates or derivation patterns.
- Have an empty or invalid name.

The initial registered-policy implementation should support native SegWit
`wsh(sortedmulti(...))`. Other BIP-388 policies can be added only with clear
review output and adversarial tests. Unsupported policies must fail closed.

Multisig users must back up the complete named policy. A seed cannot recover
the threshold, cosigners, key origins, or descriptor template.

## Airgap protocol

Thunder Den exposes two logical commands over a versioned QR transport. Each
request carries the complete data shown below; there is no interactive Merkle
proof exchange as used by Ledger's APDU transport.

```text
REGISTER_WALLET

request:
    wallet_policy

response:
    wallet_id
    wallet_hmac
```

```text
SIGN_PSBT

request:
    psbt
    wallet_policy
    wallet_hmac

response:
    signed_psbt
```

The transport must have explicit command and version fields, strict size
limits, and an unambiguous encoding. Registration and signing requests remain
separate. The final transport encoding must be documented with test vectors
before implementation.

## Registration flow

1. Scan a `REGISTER_WALLET` request.
2. Parse and validate the complete BIP-388 policy.
3. Ask for the mnemonic and optional passphrase.
4. Re-derive candidate xpubs and require an exact match for at least one key.
5. Use Bitcoin Core to validate the materialized descriptor and derive the first receive address.
6. Show the name, script type, threshold, every cosigner, owned keys, and first receive address.
7. Require explicit approval.
8. Return the Ledger-compatible wallet ID and Thunder Den registration HMAC.
9. Remove all seed and derived secret material from RAM.

## Signing flow

1. Scan a `SIGN_PSBT` request containing the PSBT, policy, and HMAC.
2. Use Bitcoin Core `decodepsbt` and `analyzepsbt` for structural and amount checks.
3. Ask for the mnemonic and optional passphrase.
4. Re-derive and exactly match the policy's local xpubs.
5. Accept an unregistered policy only when it passes every default-wallet rule.
6. Otherwise recompute and verify the registration HMAC before trusting the policy name or change rules.
7. Materialize the BIP-388 policy as Bitcoin Core descriptors, inserting private key material only for local keys.
8. Recompute wallet-owned input and change scripts instead of trusting PSBT labels.
9. Show external outputs, amounts, wallet net flow, fee, fee rate, locktime, sighash rule, and policy name.
10. Require explicit approval, then call Bitcoin Core `descriptorprocesspsbt`.
11. Verify that the unsigned transaction did not change and that a new local signature was added.
12. Return the updated PSBT and remove all secret material from RAM.

For BIP44, BIP49, BIP84, and initial multisig support, accept only
`SIGHASH_ALL`. For BIP86 key-path signing, accept only `SIGHASH_DEFAULT`.
Single-signature policies must finish complete. Multisig policies may remain
incomplete, but they must contain a newly added signature from Thunder Den.

## Dependency boundary

The target runtime remains small:

- Bitcoin Core parses, analyzes, validates, and signs PSBTs and descriptors.
- `bitcoin-bash-tools` handles BIP39 and BIP32 key derivation.
- OpenSSL provides the existing hash and HMAC primitives.
- `jq` extracts fields from Bitcoin Core JSON without a custom JSON parser.
- Thunder Den code implements policy restrictions, Ledger-compatible wallet IDs, user review, and airgap orchestration.

Do not add a second PSBT, descriptor, Miniscript, or cryptographic
implementation when Bitcoin Core or OpenSSL already supplies it.

## References

- BIP-388 wallet policies: <https://github.com/bitcoin/bips/blob/master/bip-0388.mediawiki>
- SLIP-0021 symmetric key derivation: <https://github.com/satoshilabs/slips/blob/master/slip-0021.md>
- Ledger wallet policy version 2: <https://github.com/LedgerHQ/app-bitcoin/blob/develop/doc/wallet.md>
- Ledger Merkle tree construction: <https://github.com/LedgerHQ/app-bitcoin/blob/develop/doc/merkle.md>
