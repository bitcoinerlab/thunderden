# Previous transactions and fee verification

Wallet apps such as Sparrow often omit previous transactions to make QR codes
smaller. Thunder Den refuses signing when missing data could conceal a higher
real fee. Safe compact requests remain supported; there is no warning override.

This policy is in current source. Preview 8 permitted these requests after a warning.

## What is missing?

A transaction spends outputs from earlier transactions. Each input needs its
amount and locking script. A PSBT can provide:

- **The full previous transaction** (`non_witness_utxo`). Thunder Den checks its
  transaction ID and reads the selected output from it.
- **Just the selected output** (`witness_utxo`). This is smaller, but the amount
  comes from the wallet app. Thunder Den cannot independently check that it
  belongs to the referenced transaction.

The PSBT still needs usable output information for every input. Invalid or
conflicting data is rejected.
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
amounts would invalidate signatures. Otherwise `ReviewedTransaction::Sign` rejects
the request **before approval or private-key signing**, including when some partial
signatures are already present. Every signature it may release must protect all
amounts that lack full previous transactions.

## What can a malicious wallet app do?

This is a multi-input issue, not just a multisig issue. For example, two inputs
really contain 100,000 sats each, and the transaction's outputs total 100,000 sats:

| | Actual amounts | First signing request | Second signing request |
| --- | ---: | ---: | ---: |
| Input A | 100,000 | 100,000 | 100 |
| Input B | 100,000 | 100 | 100,000 |
| Outputs | 100,000 | 100,000 | 100,000 |
| Fee | **100,000** | **100** | **100** |

Without the refusal, the first SegWit-v0 request could produce a usable signature
for A and the second for B. A malicious app could combine them into the high-fee
transaction, claiming that signing failed to persuade you to retry. Signatures
can also come from different signers or earlier sessions. Current source blocks
both requests without producing signatures.

See [BIP174's signer explanation](https://github.com/bitcoin/bips/blob/master/bip-0174.mediawiki#signer).

## What should I do?

Ask your wallet app to include full previous transactions. For Sparrow, use
**2.5.5 or later with the Krux profile**; see
[setup and troubleshooting](SPARROW.md#cannot-verify-transaction-fee).
Keeper currently has [limited compatibility](KEEPER.md).

If only a camera scan failed and the signed QR is still displayed, scan that same
QR again instead of signing anew. Multiple wallet signing passes are supported
when each pass has sufficient fee assurance.

Successful local finalization alone does not authenticate missing amounts, prove
that outputs are unspent, or establish chain acceptance.
