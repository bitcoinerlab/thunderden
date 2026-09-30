# Previous transactions and the fee warning

Wallet apps such as Sparrow often omit previous transactions to make QR codes
smaller. Thunder Den can sign compact SegWit PSBTs, but some requests need a
warning because their fee cannot be fully checked.

## What is missing?

A transaction spends outputs from earlier transactions. Each input needs its
amount and locking script. A PSBT can provide:

- **The full previous transaction** (`non_witness_utxo`). Thunder Den checks its
  transaction ID and reads the selected output from it.
- **Just the selected output** (`witness_utxo`). This is smaller, but the amount
  comes from the wallet app. Thunder Den cannot independently check that it
  belongs to the referenced transaction.

The PSBT still needs usable output information for every input. Invalid or
conflicting data is rejected; the warning cannot override those checks.
Legacy inputs still require full previous transactions.

## When are compact inputs protected?

Thunder Den uses `SIGHASH_ALL` for legacy and SegWit-v0 signatures, and
`SIGHASH_DEFAULT` for Taproot signatures.

- A SegWit-v0 signature commits to **its own input's amount** and all outputs.
  With exactly one transaction input, a false amount makes that signature
  invalid against the actual output. This also applies to one multisig input.
- A Taproot DEFAULT signature commits to **every input's amount and script**.
- With several inputs, a legacy or SegWit-v0 signature may leave other input
  amounts unchecked. Those amounts need full previous transactions for an
  independently checked fee.

Thunder Den checks this for the selected wallet's eligible signing inputs. Where
their commitments cover every missing amount, the review explains that incorrect
amounts would invalidate signatures. Otherwise it conservatively shows the red
fee warning, including when some partial signatures are already present.

## What can a malicious wallet app do?

This is a multi-input issue, not just a multisig issue. For example, two inputs
really contain 100,000 sats each, and the transaction's outputs total 100,000 sats:

| | Actual amounts | First signing request | Second signing request |
| --- | ---: | ---: | ---: |
| Input A | 100,000 | 100,000 | 100 |
| Input B | 100,000 | 100 | 100,000 |
| Outputs | 100,000 | 100,000 | 100,000 |
| Fee | **100,000** | **100** | **100** |

For SegWit v0, the first request can produce a usable signature for A and the
second a usable signature for B. A malicious app can combine them into the
high-fee transaction. It may claim that signing failed to persuade you to retry.
Signatures can also come from different signers or earlier sessions; seeing the
warning only once is not proof of safety.

See [BIP174's signer explanation](https://github.com/bitcoin/bips/blob/master/bip-0174.mediawiki#signer).

## What should I do?

If your app unexpectedly reports a signing error and asks you to sign again,
stop and check what happened before retrying. If only the camera scan failed and
the signed QR is still displayed, scan that same QR again instead of signing anew.

Choose **Continue to review** only if you accept the fee-verification limitation.
The review keeps the fee and affected totals marked **unverified**, including the
final confirmation and the saved review beside the reply QR. Press Esc to cancel.
Continuing does not authenticate the missing amounts or prevent the attack.

Some workflows legitimately need several signing passes, including
[transactions using more than one wallet](SPARROW.md#transactions-using-more-than-one-wallet).
An expected next pass is different from an unexplained error asking you to retry.

If your wallet app can include full previous transactions, that avoids relying on
unverified amounts. Successful local finalization alone does not authenticate
missing data, prove that outputs are unspent, or establish chain acceptance.
